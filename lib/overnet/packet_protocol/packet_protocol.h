// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <map>
#include "garnet/lib/overnet/environment/timer.h"
#include "garnet/lib/overnet/environment/trace.h"
#include "garnet/lib/overnet/labels/seq_num.h"
#include "garnet/lib/overnet/packet_protocol/bbr.h"
#include "garnet/lib/overnet/protocol/ack_frame.h"
#include "garnet/lib/overnet/protocol/varint.h"
#include "garnet/lib/overnet/vocabulary/callback.h"
#include "garnet/lib/overnet/vocabulary/lazy_slice.h"
#include "garnet/lib/overnet/vocabulary/once_fn.h"
#include "garnet/lib/overnet/vocabulary/optional.h"
#include "garnet/lib/overnet/vocabulary/slice.h"
#include "garnet/lib/overnet/vocabulary/status.h"

// Enable for indepth refcount debugging for packet protocol ops.
//#define OVERNET_TRACE_PACKET_PROTOCOL_OPS

namespace overnet {

class PacketProtocol {
 public:
  static constexpr inline auto kModule = Module::PACKET_PROTOCOL;

  class PacketSender {
   public:
    virtual void SendPacket(SeqNum seq, LazySlice data,
                            Callback<void> done) = 0;
  };

  class Codec {
   public:
    // How much will this codec expand a message? (maximums only).
    const Border border;
    virtual StatusOr<Slice> Encode(uint64_t seq_idx, Slice src) const = 0;
    virtual StatusOr<Slice> Decode(uint64_t seq_idx, Slice src) const = 0;

   protected:
    explicit Codec(Border border) : border(border) {}
  };

  class SendRequestHdl;

  class SendRequest {
   public:
    // Called at most once, and always before Ack.
    virtual Slice GenerateBytes(LazySliceArgs args) = 0;
    // Called exactly once.
    virtual void Ack(const Status& status) = 0;

    template <class GB, class A>
    static SendRequestHdl FromFunctors(GB gb, A a);
  };

  class SendRequestHdl {
   public:
    explicit SendRequestHdl(SendRequest* req) : req_(req) {}
    SendRequestHdl() : SendRequestHdl(nullptr) {}
    SendRequestHdl(const SendRequestHdl&) = delete;
    SendRequestHdl& operator=(const SendRequestHdl&) = delete;
    SendRequestHdl(SendRequestHdl&& other) : req_(other.req_) {
      other.req_ = nullptr;
    }
    SendRequestHdl& operator=(SendRequestHdl&& other) {
      this->~SendRequestHdl();
      req_ = other.req_;
      other.req_ = nullptr;
      return *this;
    }
    ~SendRequestHdl() {
      if (req_) {
        req_->Ack(Status::Cancelled());
      }
    }

    bool empty() const { return req_ == nullptr; }

    Slice GenerateBytes(LazySliceArgs args) {
      return req_->GenerateBytes(args);
    }

    void Ack(const Status& status) {
      SendRequest* req = req_;
      req_ = nullptr;
      req->Ack(status);
    }

    SendRequest* borrow() { return req_; }

   private:
    SendRequest* req_;
  };

  static Codec* NullCodec();

  static constexpr size_t kMaxUnackedReceives = 3;
  using RandFunc = BBR::RandFunc;

  PacketProtocol(Timer* timer, RandFunc rand, PacketSender* packet_sender,
                 const Codec* codec, uint64_t mss)
      : timer_(timer),
        packet_sender_(packet_sender),
        codec_(codec),
        mss_(mss),
        outgoing_bbr_(timer_, rand, mss_, Nothing),
        ack_only_send_request_(this) {}

  void Close(Callback<void> quiesced);

  ~PacketProtocol() { assert(state_ == State::CLOSED); }

  uint32_t mss() const {
    auto codec_expansion = codec_->border.prefix + codec_->border.suffix;
    if (codec_expansion > mss_)
      return 0;
    return mss_ - codec_expansion;
  }

  void Send(SendRequestHdl request);
  template <class GB, class A>
  void Send(GB gb, A a) {
    Send(SendRequest::FromFunctors(std::move(gb), std::move(a)));
  }
  void RequestSendAck();

  Bandwidth BottleneckBandwidth() {
    return outgoing_bbr_.bottleneck_bandwidth();
  }

  TimeDelta RoundTripTime() { return outgoing_bbr_.rtt(); }

 private:
  // Placing an OutstandingOp on a PacketProtocol object prevents it from
  // quiescing
  template <const char* kWTF>
  class OutstandingOp {
   public:
    OutstandingOp() = delete;
    OutstandingOp(PacketProtocol* pp) : pp_(pp) { pp_->BeginOp(why(), this); }
    OutstandingOp(const OutstandingOp& other) : pp_(other.pp_) {
      pp_->BeginOp(why(), this);
    }
    OutstandingOp& operator=(OutstandingOp other) {
      other.Swap(this);
      return *this;
    }
    void Swap(OutstandingOp* other) { std::swap(pp_, other->pp_); }

    const char* why() { return kWTF; }

    ~OutstandingOp() { pp_->EndOp(why(), this); }
    PacketProtocol* operator->() const { return pp_; }
    PacketProtocol* get() const { return pp_; }

   private:
    PacketProtocol* pp_;
  };

  enum class ReceiveState : uint8_t {
    UNKNOWN,
    NOT_RECEIVED,
    RECEIVED,
    RECEIVED_AND_SUPPRESSED_ACK
  };

  struct AckActions {
    AckActions() = default;
    AckActions(const AckActions&) = delete;
    AckActions& operator=(const AckActions&) = delete;
    AckActions(AckActions&&) = default;
    AckActions& operator=(AckActions&&) = default;

    std::vector<SendRequestHdl> acks;
    std::vector<SendRequestHdl> nacks;
    Optional<BBR::Ack> bbr_ack;
  };

  void RunAckActions(AckActions* ack_actions, const Status& status);

 public:
  static const char kProcessedPacket[];

  class ProcessedPacket {
   public:
    ProcessedPacket(const ProcessedPacket&) = delete;
    ProcessedPacket& operator=(const ProcessedPacket&) = delete;

    StatusOr<Optional<Slice>> status;

    // Force this packet to be nacked
    void Nack();

    ~ProcessedPacket();

   private:
    enum class SendAck : uint8_t { NONE, FORCE, SCHEDULE };

    friend class PacketProtocol;
    ProcessedPacket(OutstandingOp<kProcessedPacket> protocol, uint64_t seq_idx,
                    SendAck send_ack, ReceiveState final_receive_state,
                    StatusOr<Optional<Slice>> result,
                    Optional<AckActions> ack_actions)
        : status(std::move(result)),
          seq_idx_(seq_idx),
          final_receive_state_(final_receive_state),
          send_ack_(send_ack),
          protocol_(protocol),
          ack_actions_(std::move(ack_actions)) {}

    uint64_t seq_idx_;
    ReceiveState final_receive_state_;
    SendAck send_ack_;
    OutstandingOp<kProcessedPacket> protocol_;
    Optional<AckActions> ack_actions_;
  };

  ProcessedPacket Process(TimeStamp received, SeqNum seq, Slice slice);

 private:
  enum class OutstandingPacketState : uint8_t {
    PENDING,
    SENT,
    ACKED,
    NACKED,
  };

  struct OutstandingPacket {
    TimeStamp scheduled;
    OutstandingPacketState state;
    bool has_ack;
    bool is_pure_ack;
    uint64_t ack_to_seq;
    Optional<BBR::SentPacket> bbr_sent_packet;
    SendRequestHdl request;
  };

  struct QueuedPacket {
    Op op;
    SendRequestHdl request;
  };

  bool AckIsNeeded() const;
  TimeDelta QuarterRTT() const;
  void MaybeForceAck();
  void MaybeScheduleAck();
  void MaybeSendAck();
  void MaybeSendSlice(QueuedPacket&& packet);
  void SendSlice(QueuedPacket&& packet);
  void TransmitPacket();
  StatusOr<AckActions> HandleAck(const AckFrame& ack, bool is_synthetic);
  void ContinueSending();
  void KeepAlive();
  TimeStamp RetransmissionDeadline() const;
  void ScheduleRTO();
  void NackBefore(TimeStamp epoch, const Status& nack_status);
  std::string AckDebugText();
  void BeginOp(const char* name, void* whom) {
#ifdef OVERNET_TRACE_PACKET_PROTOCOL_OPS
    ScopedModule<PacketProtocol> mod(this);
    OVERNET_TRACE(DEBUG) << " BEG " << name << " " << whom << " "
                         << outstanding_ops_ << " -> "
                         << (outstanding_ops_ + 1);
#endif
    ++outstanding_ops_;
  }
  void EndOp(const char* name, void* whom) {
#ifdef OVERNET_TRACE_PACKET_PROTOCOL_OPS
    ScopedModule<PacketProtocol> mod(this);
    OVERNET_TRACE(DEBUG) << " END " << name << " " << whom << " "
                         << outstanding_ops_ << " -> "
                         << (outstanding_ops_ - 1);
#endif
    if (0 == --outstanding_ops_ && state_ == State::CLOSING) {
      state_ = State::CLOSED;
      auto cb = std::move(quiesced_);
      cb();
    }
  }

  Optional<AckFrame> GenerateAck(uint32_t max_length);
  Optional<AckFrame> GenerateAckTo(TimeStamp now, uint64_t max_seen);
  struct GeneratedPacket {
    Slice payload;
    bool has_ack;
    bool is_pure_ack;
  };
  GeneratedPacket GeneratePacket(SendRequest* send_request, LazySliceArgs args);
  Optional<uint64_t> LastRTOableSequence(TimeStamp epoch);
  TimeDelta DelayForReceivedPacket(TimeStamp now, uint64_t seq_idx);

  Timer* const timer_;
  PacketSender* const packet_sender_;
  const Codec* const codec_;
  const uint64_t mss_;

  enum class State { READY, CLOSING, CLOSED };

  State state_ = State::READY;
  Callback<void> quiesced_;

  BBR outgoing_bbr_;

  // TODO(ctiller): can we move to a ring buffer here? - the idea would be to
  // just finished(RESOURCE_EXHAUSTED) if the ring is full
  uint64_t send_tip_ = 1;
  std::deque<OutstandingPacket> outstanding_;
  std::deque<QueuedPacket> queued_;
  Optional<QueuedPacket> sending_;
  bool transmitting_ = false;

  uint64_t recv_tip_ = 0;
  uint64_t max_seen_ = 0;
  uint64_t max_acked_ = 0;
  uint64_t max_outstanding_size_ = 0;
  uint64_t last_sent_ack_ = 0;

  // TODO(ctiller): Find a more efficient data structure.
  struct ReceivedPacket {
    ReceiveState state;
    TimeStamp when;
  };
  std::map<uint64_t, ReceivedPacket> received_packets_;

  TimeStamp last_keepalive_event_ = TimeStamp::Epoch();
  TimeStamp last_ack_send_ = TimeStamp::Epoch();
  bool ack_after_sending_ = false;
  bool ack_only_message_outstanding_ = false;
  bool sent_ack_ = false;

  int outstanding_ops_ = 0;

  Optional<Timeout> ack_scheduler_;
  Optional<Timeout> rto_scheduler_;

  class AckOnlySendRequest : public SendRequest {
   public:
    AckOnlySendRequest(PacketProtocol* pp) : pp_(pp) {}
    AckOnlySendRequest(const AckOnlySendRequest&) = delete;
    AckOnlySendRequest& operator=(const AckOnlySendRequest&) = delete;
    Slice GenerateBytes(LazySliceArgs args) {
      pp_->ack_only_message_outstanding_ = false;
      return Slice();
    }
    void Ack(const Status& status) {}

   private:
    PacketProtocol* const pp_;
  };

  AckOnlySendRequest ack_only_send_request_;
};

template <class GB, class A>
PacketProtocol::SendRequestHdl PacketProtocol::SendRequest::FromFunctors(GB gb,
                                                                         A a) {
  class Send final : public SendRequest {
   public:
    Send(GB generate_bytes, A ack)
        : generate_bytes_(std::move(generate_bytes)), ack_(std::move(ack)) {}

    Slice GenerateBytes(LazySliceArgs args) { return generate_bytes_(args); }

    void Ack(const Status& status) {
      ack_(status);
      delete this;
    }

   private:
    GB generate_bytes_;
    A ack_;
  };

  return SendRequestHdl(new Send(std::move(gb), std::move(a)));
}

}  // namespace overnet
