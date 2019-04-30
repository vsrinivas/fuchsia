// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <map>
#include <queue>

#include "src/connectivity/overnet/lib/environment/timer.h"
#include "src/connectivity/overnet/lib/environment/trace.h"
#include "src/connectivity/overnet/lib/labels/seq_num.h"
#include "src/connectivity/overnet/lib/packet_protocol/bbr.h"
#include "src/connectivity/overnet/lib/protocol/ack_frame.h"
#include "src/connectivity/overnet/lib/protocol/varint.h"
#include "src/connectivity/overnet/lib/vocabulary/callback.h"
#include "src/connectivity/overnet/lib/vocabulary/lazy_slice.h"
#include "src/connectivity/overnet/lib/vocabulary/once_fn.h"
#include "src/connectivity/overnet/lib/vocabulary/optional.h"
#include "src/connectivity/overnet/lib/vocabulary/slice.h"
#include "src/connectivity/overnet/lib/vocabulary/status.h"

// Enable for indepth refcount debugging for packet protocol ops.
//#define OVERNET_TRACE_PACKET_PROTOCOL_OPS

namespace overnet {

class PacketProtocol {
 public:
  static constexpr inline auto kModule = Module::PACKET_PROTOCOL;
  static constexpr size_t kMaxUnackedReceives = 3;

  /////////////////////////////////////////////////////////////////////////////
  // Collaborating types.

 public:
  // PacketSender defines how a packet protocol sends it's data.
  class PacketSender {
   public:
    virtual void SendPacket(SeqNum seq, LazySlice send) = 0;
  };

  // Codec describes the transformation to apply to *payload* bytes that are
  // sent.
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

  // SendRequest is a request to send one message, and an ack function for when
  // it's received.
  class SendRequest {
   public:
    SendRequest(bool must_send_ack = false) : must_send_ack_(must_send_ack) {}

    // Called at most once, and always before Ack.
    virtual Slice GenerateBytes(LazySliceArgs args) = 0;
    // Called exactly once.
    virtual void Ack(const Status& status) = 0;

    bool must_send_ack() const { return must_send_ack_; }

    template <class GB, class A>
    static SendRequestHdl FromFunctors(GB gb, A a);

   private:
    const bool must_send_ack_;
  };

  // A smart pointer around SendRequest. The held request must stay valid until
  // Ack() is called.
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

  // A smart pointer to prevent a PacketProtocol from quiescing until it's
  // either explicitly dropped or destroyed.
  class ProtocolRef {
   public:
    ProtocolRef(PacketProtocol* protocol) : protocol_(protocol) {
      protocol_->refs_++;
    }
    ~ProtocolRef() {
      if (protocol_) {
        Drop();
      }
    }

    void Drop() {
      assert(protocol_ != nullptr);
      auto protocol = protocol_;
      protocol_ = nullptr;
      if (0 == --protocol->refs_) {
        auto cb = std::move(protocol->quiesce_);
        cb();
      }
    }

    ProtocolRef(const ProtocolRef& other) = delete;
    ProtocolRef& operator=(const ProtocolRef& other) = delete;

    ProtocolRef(ProtocolRef&& other) : protocol_(other.protocol_) {
      other.protocol_ = nullptr;
    }
    ProtocolRef& operator=(ProtocolRef&& other) {
      std::swap(protocol_, other.protocol_);
      return *this;
    }

    PacketProtocol* operator->() const { return protocol_; }
    PacketProtocol* get() const { return protocol_; }

    bool has_value() const { return protocol_ != nullptr; }

   private:
    PacketProtocol* protocol_;
  };

  /////////////////////////////////////////////////////////////////////////////
  // Internal interfaces.

 private:
  enum class ProcessMessageResult : uint8_t {
    NOT_PROCESSED,
    NACK,
    OPTIONAL_ACK,
    ACK,
    ACK_URGENTLY,
  };

  friend std::ostream& operator<<(std::ostream& out, ProcessMessageResult pmr) {
    switch (pmr) {
      case ProcessMessageResult::NOT_PROCESSED:
        return out << "NOT_PROCESSED";
      case ProcessMessageResult::NACK:
        return out << "NACK";
      case ProcessMessageResult::ACK:
        return out << "ACK";
      case ProcessMessageResult::ACK_URGENTLY:
        return out << "ACK_URGENTLY";
      case ProcessMessageResult::OPTIONAL_ACK:
        return out << "OPTIONAL_ACK";
    }
    abort();
  }

  class PacketSend;

  // OutstandingMessages tracks messages that are sent but not yet acknowledged.
  class OutstandingMessages {
   public:
    OutstandingMessages(PacketProtocol* protocol);
    ~OutstandingMessages() { NackAll(); }
    void Send(BBR::TransmitRequest bbr_request, SendRequestHdl request);
    Status ValidateAck(const AckFrame& ack) const;
    void ProcessValidAck(AckFrame ack, TimeStamp received);
    void ReceivedPacket() { ScheduleRetransmit(); }

   private:
    friend class PacketSend;

    Slice GeneratePacket(BBR::TransmitRequest bbr_request, uint64_t seq_idx,
                         LazySliceArgs args);
    void CancelPacket(uint64_t seq_idx);
    void FinishedSending();
    void ScheduleRetransmit();
    Optional<TimeStamp> RetransmitDeadline();
    void CheckRetransmit();
    void NackAll();

    enum class OutstandingPacketState : uint8_t {
      PENDING,
      SENT,
      ACKED,
      NACKED,
    };

    friend std::ostream& operator<<(std::ostream& out,
                                    OutstandingPacketState state) {
      switch (state) {
        case OutstandingPacketState::PENDING:
          return out << "PENDING";
        case OutstandingPacketState::SENT:
          return out << "SENT";
        case OutstandingPacketState::ACKED:
          return out << "ACKED";
        case OutstandingPacketState::NACKED:
          return out << "NACKED";
      }
    }

    struct OutstandingPacket {
      OutstandingPacketState state;
      uint64_t max_seen_sequence_at_send;
      Optional<BBR::SentPacket> bbr_sent_packet;
      SendRequestHdl request;
    };

    // Assists processing a set of acks/nacks
    class AckProcessor {
     public:
      AckProcessor(OutstandingMessages* outstanding, TimeDelta queue_delay)
          : outstanding_(outstanding), queue_delay_(queue_delay) {}
      ~AckProcessor();

      void Ack(uint64_t seq);
      void Nack(uint64_t seq, const Status& status);

     private:
      OutstandingMessages* const outstanding_;
      const TimeDelta queue_delay_;
      BBR::Ack bbr_ack_;
    };

    PacketProtocol* const protocol_;
    uint64_t send_tip_ = 1;
    uint64_t max_outstanding_size_ = 1;
    uint64_t last_sent_ack_ = 0;
    std::deque<OutstandingPacket> outstanding_;

    Optional<Timeout> rto_timer_;
  };

  // SendQueue groups together pending sends in the protocol and manages writing
  // them out.
  class SendQueue {
   public:
    SendQueue(PacketProtocol* protocol) : protocol_(protocol) {}

    // Schedule this queue to be sent. Should only be used as directed.
    void Schedule();

    // A message was just sent: possibly schedule the next one.
    void SentMessage();

    // Ensure a tail loss probe is scheduled: used to force acks to be sent on
    // idle connections.
    void ScheduleTailLossProbe();

    // Force a packet to be sent with an ack.
    void ForceSendAck();

    // Returns true if send request addition triggers the need for
    // Schedule to be called.
    [[nodiscard]] bool Add(SendRequestHdl hdl);

   private:
    PacketProtocol* const protocol_;
    bool scheduled_ = false;
    bool last_send_was_tail_loss_probe_ = false;
    Optional<BBR::TransmitRequest> transmit_request_;
    std::queue<SendRequestHdl> requests_;
    struct ScheduledTailLossProbe {
      template <class F>
      ScheduledTailLossProbe(Timer* timer, TimeStamp when, F f)
          : when(when), timeout(timer, when, std::move(f)) {}
      TimeStamp when;
      Timeout timeout;
    };
    Optional<ScheduledTailLossProbe> scheduled_tail_loss_probe_;
  };

  enum class AckUrgency { NOT_REQUIRED, SEND_SOON, SEND_IMMEDIATELY };

  friend std::ostream& operator<<(std::ostream& out, AckUrgency urgency) {
    switch (urgency) {
      case AckUrgency::NOT_REQUIRED:
        return out << "NOT_REQUIRED";
      case AckUrgency::SEND_SOON:
        return out << "SEND_SOON";
      case AckUrgency::SEND_IMMEDIATELY:
        return out << "SEND_IMMEDIATELY";
    }
  }

  class AckSender {
   public:
    AckSender(PacketProtocol* protocol);

    void NeedAck(AckUrgency urgency);
    bool ShouldSendAck() const {
      return !all_acks_acknowledged_ && sent_full_acks_.empty();
    }
    void AckSent(uint64_t seq_idx, bool partial);
    void OnNack(uint64_t seq_idx);
    void OnAck(uint64_t seq_idx);

   private:
    PacketProtocol* const protocol_;
    std::vector<uint64_t> sent_full_acks_;
    bool all_acks_acknowledged_ = true;
    AckUrgency urgency_ = AckUrgency::NOT_REQUIRED;

    std::string SentFullAcksString() const;
  };

  // ReceivedQueue tracks which packets we've received (or not), and state we
  // need to acknowledge them.
  class ReceivedQueue {
   public:
    // Return true if an ack should be sent now.
    template <class F>
    [[nodiscard]] AckUrgency Received(SeqNum seq_num, TimeStamp received,
                                      F logic);

    uint64_t max_seen_sequence() const;
    bool CanBuildAck() const { return max_seen_sequence() > 0; }
    AckFrame BuildAck(uint64_t seq_idx, TimeStamp now, uint32_t max_length,
                      AckSender* ack_sender);
    void SetTip(uint64_t seq_idx, TimeStamp received);

   private:
    [[nodiscard]] bool EnsureValidReceivedPacket(uint64_t seq_idx,
                                                 TimeStamp received) {
      constexpr uint64_t kMaxSkip = 65536;
      if (seq_idx > received_tip_ && seq_idx - received_tip_ > kMaxSkip) {
        return false;
      }
      while (received_tip_ + received_packets_.size() <= seq_idx) {
        received_packets_.emplace_back(
            ReceivedPacket{ReceiveState::UNKNOWN, received});
      }
      return true;
    }
    std::string ReceivedPacketSummary() const;

    uint64_t received_tip_ = 0;
    uint64_t optional_ack_run_length_ = 0;

    enum class ReceiveState {
      UNKNOWN,
      NOT_RECEIVED,
      RECEIVED,
    };

    friend std::ostream& operator<<(std::ostream& out, ReceiveState state) {
      switch (state) {
        case ReceiveState::UNKNOWN:
          return out << "UNKNOWN";
        case ReceiveState::NOT_RECEIVED:
          return out << "NOT_RECEIVED";
        case ReceiveState::RECEIVED:
          return out << "RECEIVED";
      }
    }

    // TODO(ctiller): Find a more efficient data structure.
    struct ReceivedPacket {
      ReceiveState state;
      TimeStamp when;
    };
    std::deque<ReceivedPacket> received_packets_;
  };

  // A Transaction is created to describe a set of changes to a PacketProtocol.
  // One is created in response to every Process() call, and in response to
  // sends that are outside of incoming messages.
  // Only one Transaction can be active at a time.
  // A Transaction can process only one incoming message.
  // A Transaction may process any number (including zero) sends.
  class Transaction {
   public:
    Transaction(PacketProtocol* protocol);
    ~Transaction();
    void Send(SendRequestHdl hdl);
    void ScheduleForcedAck() { schedule_send_queue_ = true; }

    void QuiesceOnCompletion(Callback<void> callback);

    bool Closing() const { return quiesce_ || !protocol_->state_.has_value(); }

   private:
    SendQueue* send_queue();

    PacketProtocol* const protocol_;
    bool schedule_send_queue_ = false;
    bool quiesce_ = false;
  };

  class PacketSend final {
   public:
    PacketSend(PacketProtocol* protocol, uint64_t seq_idx,
               BBR::TransmitRequest bbr_request);
    ~PacketSend();
    PacketSend(const PacketSend&) = delete;
    PacketSend(PacketSend&&) = default;

    Slice operator()(LazySliceArgs args);

   private:
    ProtocolRef protocol_;
    uint64_t seq_idx_;
    BBR::TransmitRequest bbr_request_;
  };

  /////////////////////////////////////////////////////////////////////////////
  // PacketProtocol interface

 public:
  using RandFunc = BBR::RandFunc;

  PacketProtocol(Timer* timer, RandFunc rand, PacketSender* packet_sender,
                 const Codec* codec, uint64_t mss);

  // Request that a single message be sent.
  void Send(SendRequestHdl send_request);

  template <class GB, class A>
  void Send(GB gb, A a) {
    Send(SendRequest::FromFunctors(std::move(gb), std::move(a)));
  }

  class IncomingMessage {
   public:
    explicit IncomingMessage(Slice payload) : payload(std::move(payload)) {}
    IncomingMessage(const IncomingMessage&) = delete;
    IncomingMessage& operator=(const IncomingMessage&) = delete;

    Slice payload;
    void Nack() { nack_ = true; }

    bool was_nacked() const { return nack_; }

   private:
    bool nack_ = false;
  };

  using ProcessCallback =
      Callback<StatusOr<IncomingMessage*>, 4 * sizeof(void*)>;

  void Process(TimeStamp received, SeqNum seq, Slice slice,
               ProcessCallback handle_message);

  void Close(Callback<void> quiesced);

  uint32_t maximum_send_size() const { return maximum_send_size_; }
  TimeDelta round_trip_time() const {
    return state_.has_value() ? state_->bbr_.rtt() : TimeDelta::PositiveInf();
  }
  Bandwidth bottleneck_bandwidth() const {
    return state_.has_value() ? state_->bbr_.bottleneck_bandwidth()
                              : Bandwidth::Zero();
  }

  static Codec* NullCodec();

  /////////////////////////////////////////////////////////////////////////////
  // Internal methods.
 private:
  TimeDelta CurrentRTT() const;
  TimeDelta RetransmitDelay() const;
  TimeDelta TailLossProbeDelay() const;
  Slice FormatPacket(uint64_t seq_idx, SendRequest* request,
                     LazySliceArgs args);
  ProcessMessageResult ProcessMessage(uint64_t seq_idx, Slice slice,
                                      TimeStamp received,
                                      ProcessCallback handle_message);
  // Run closure f in a transaction (creating one if necessary)
  template <class F>
  auto InTransaction(F f) {
    if (active_transaction_ != nullptr) {
      return f(active_transaction_);
    } else {
      Transaction transaction(this);
      return f(&transaction);
    }
  }
  void Quiesce();

  /////////////////////////////////////////////////////////////////////////////
  // Internal state.

 private:
  const Codec* const codec_;
  Timer* const timer_;
  PacketSender* const packet_sender_;
  Transaction* active_transaction_ = nullptr;
  Callback<void> quiesce_;
  const uint32_t maximum_send_size_;
  uint32_t refs_ = 0;
  ProtocolRef master_ref_{this};
  struct OpenState {
    OpenState(PacketProtocol* protocol, RandFunc rand)
        : ack_sender(protocol),
          outstanding_messages(protocol),
          bbr_(protocol->timer_, std::move(rand), protocol->maximum_send_size_,
               Nothing) {}
    AckSender ack_sender;
    Optional<SendQueue> send_queue;
    ReceivedQueue received_queue;
    OutstandingMessages outstanding_messages;
    BBR bbr_;
  };
  Optional<OpenState> state_;
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
