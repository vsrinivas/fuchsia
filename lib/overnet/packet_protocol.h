// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <map>
#include "ack_frame.h"
#include "bbr.h"
#include "callback.h"
#include "lazy_slice.h"
#include "once_fn.h"
#include "optional.h"
#include "seq_num.h"
#include "slice.h"
#include "status.h"
#include "timer.h"
#include "trace.h"
#include "varint.h"

// Enable for indepth refcount debugging for packet protocol ops.
// #define OVERNET_TRACE_PACKET_PROTOCOL_OPS

namespace overnet {

class PacketProtocol {
 public:
  class PacketSender {
   public:
    virtual void SendPacket(SeqNum seq, LazySlice data,
                            Callback<void> done) = 0;
  };

  static constexpr size_t kMaxUnackedReceives = 3;

  PacketProtocol(Timer* timer, PacketSender* packet_sender,
                 TraceSink trace_sink, uint64_t mss)
      : timer_(timer),
        packet_sender_(packet_sender),
        trace_sink_(trace_sink.Decorate([this](const std::string& msg) {
          std::ostringstream out;
          out << "PktProto[" << this << "] " << msg;
          return out.str();
        })),
        mss_(mss),
        outgoing_bbr_(timer_, trace_sink_, mss_, Nothing) {}

  void Close(Callback<void> quiesced);

  ~PacketProtocol() { assert(state_ == State::CLOSED); }

  uint32_t mss() const { return mss_; }

  using SendCallback = Callback<Status, 16 * sizeof(void*)>;

  void Send(LazySlice make_payload, SendCallback on_ack);

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

 public:
  static const char kProcessedPacket[];

  class ProcessedPacket {
   public:
    ProcessedPacket(const ProcessedPacket&) = delete;
    ProcessedPacket& operator=(const ProcessedPacket&) = delete;
    ProcessedPacket(ProcessedPacket&&) = default;
    ProcessedPacket& operator=(ProcessedPacket&&) = default;

    StatusOr<Optional<Slice>> status;

    ~ProcessedPacket() {
      switch (ack_) {
        case Ack::NONE:
          break;
        case Ack::FORCE:
          protocol_->MaybeForceAck();
          break;
        case Ack::SCHEDULE:
          protocol_->MaybeScheduleAck();
          break;
      }
    }

   private:
    enum class Ack { NONE, FORCE, SCHEDULE };

    friend class PacketProtocol;
    ProcessedPacket(OutstandingOp<kProcessedPacket> protocol, Ack ack,
                    StatusOr<Optional<Slice>> result)
        : status(std::move(result)), ack_(ack), protocol_(protocol) {}

    Ack ack_;
    OutstandingOp<kProcessedPacket> protocol_;
  };

  ProcessedPacket Process(TimeStamp received, SeqNum seq, Slice slice);

 private:
  struct OutstandingPacket {
    uint64_t ack_to_seq;
    Optional<BBR::SentPacket> bbr_sent_packet;
    SendCallback on_ack;
  };

  struct QueuedPacket {
    LazySlice payload_factory;
    SendCallback on_ack;
  };

  bool AckIsNeeded() const;
  TimeDelta QuarterRTT() const;
  void MaybeForceAck();
  void MaybeScheduleAck();
  void MaybeSendAck();
  void MaybeSendSlice(QueuedPacket&& packet);
  void SendSlice(QueuedPacket&& packet);
  void TransmitPacket();
  Status HandleAck(const AckFrame& ack);
  void ContinueSending();
  void KeepAlive();
  TimeStamp RetransmissionDeadline() const;
  void ScheduleRTO();
  void NackAll();
  void BeginOp(const char* name, void* whom) {
#ifdef OVERNET_TRACE_PACKET_PROTOCOL_OPS
    OVERNET_TRACE(DEBUG, trace_sink_) << " BEG " << name << " " << whom;
#endif
    ++outstanding_ops_;
  }
  void EndOp(const char* name, void* whom) {
#ifdef OVERNET_TRACE_PACKET_PROTOCOL_OPS
    OVERNET_TRACE(DEBUG, trace_sink_) << " END " << name << " " << whom;
#endif
    if (0 == --outstanding_ops_ && state_ == State::CLOSING) {
      state_ = State::CLOSED;
      auto cb = std::move(quiesced_);
      cb();
    }
  }

  Optional<AckFrame> GenerateAck();
  Slice GeneratePacket(LazySlice payload, LazySliceArgs args);

  Timer* const timer_;
  PacketSender* const packet_sender_;
  const TraceSink trace_sink_;
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

  uint64_t recv_tip_ = 0;
  uint64_t max_seen_ = 0;
  TimeStamp max_seen_time_ = TimeStamp::Epoch();
  uint64_t max_acked_ = 0;
  uint64_t max_outstanding_size_ = 0;

  // TODO(ctiller): Find a more efficient data structure.
  struct ReceivedPacket {
    bool received;
    bool suppressed_ack;
  };
  std::map<uint64_t, ReceivedPacket> received_packets_;

  TimeStamp last_keepalive_event_ = TimeStamp::Epoch();
  TimeStamp last_ack_send_ = TimeStamp::Epoch();
  bool ack_after_sending_ = false;

  int outstanding_ops_ = 0;

  Optional<Timeout> ack_scheduler_;
  Optional<Timeout> rto_scheduler_;
};

}  // namespace overnet
