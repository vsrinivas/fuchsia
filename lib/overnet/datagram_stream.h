// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>  // TODO(ctiller): switch to a short queue (inlined 1-2 elems, linked list)
#include "ack_frame.h"
#include "internal_list.h"
#include "linearizer.h"
#include "packet_protocol.h"
#include "receive_mode.h"
#include "reliability_and_ordering.h"
#include "router.h"
#include "seq_num.h"
#include "sink.h"
#include "slice.h"
#include "timer.h"
#include "trace.h"

namespace overnet {

class MessageFragment {
 public:
  enum class Type : uint8_t { Chunk = 0, MessageAbort = 1, StreamEnd = 2 };

  MessageFragment(const MessageFragment&) = delete;
  MessageFragment& operator=(const MessageFragment&) = delete;

  MessageFragment(uint64_t message, Chunk chunk)
      : message_(message), type_(Type::Chunk) {
    assert(message > 0);
    new (&payload_.chunk) Chunk(std::move(chunk));
  }

  MessageFragment(MessageFragment&& other)
      : message_(other.message_), type_(other.type_) {
    switch (type_) {
      case Type::Chunk:
        new (&payload_.chunk) Chunk(std::move(other.payload_.chunk));
        break;
      case Type::MessageAbort:
      case Type::StreamEnd:
        new (&payload_.status) Status(std::move(other.payload_.status));
        break;
    }
  }

  MessageFragment& operator=(MessageFragment&& other) {
    if (type_ != other.type_) {
      this->~MessageFragment();
      new (this) MessageFragment(std::forward<MessageFragment>(other));
    } else {
      switch (type_) {
        case Type::Chunk:
          payload_.chunk = std::move(other.payload_.chunk);
          break;
        case Type::MessageAbort:
        case Type::StreamEnd:
          payload_.status = std::move(other.payload_.status);
          break;
      }
    }
    return *this;
  }

  ~MessageFragment() {
    switch (type_) {
      case Type::Chunk:
        payload_.chunk.~Chunk();
        break;
      case Type::MessageAbort:
      case Type::StreamEnd:
        payload_.status.~Status();
        break;
    }
  }

  static MessageFragment Abort(uint64_t message_id, Status status) {
    return MessageFragment(message_id, Type::MessageAbort, std::move(status));
  }

  static MessageFragment EndOfStream(uint64_t last_message_id, Status status) {
    return MessageFragment(last_message_id, Type::StreamEnd, std::move(status));
  }

  Slice Write(uint64_t desired_prefix) const;
  static StatusOr<MessageFragment> Parse(Slice incoming);

  Type type() const { return type_; }

  uint64_t message() const { return message_; }

  const Chunk& chunk() const {
    assert(type_ == Type::Chunk);
    return payload_.chunk;
  }
  Chunk* mutable_chunk() {
    assert(type_ == Type::Chunk);
    return &payload_.chunk;
  }

  const Status& status() const {
    assert(type_ == Type::MessageAbort || type_ == Type::StreamEnd);
    return payload_.status;
  }

 private:
  static constexpr uint8_t kFlagEndOfMessage = 0x80;
  static constexpr uint8_t kFlagTypeMask = 0x0f;
  static constexpr uint8_t kReservedFlags =
      static_cast<uint8_t>(~(kFlagTypeMask | kFlagEndOfMessage));

  MessageFragment(uint64_t message, Type type, Status status)
      : message_(message), type_(type) {
    assert(type == Type::MessageAbort || type == Type::StreamEnd);
    assert(message != 0);
    new (&payload_.status) Status(std::move(status));
  }

  uint64_t message_;
  union Payload {
    Payload() {}
    ~Payload() {}

    Chunk chunk;
    Status status;
  };
  Type type_;
  Payload payload_;
};

class DatagramStream : private Router::StreamHandler,
                       private PacketProtocol::PacketSender {
  class IncomingMessage {
   public:
    // TODO(ctiller): 1MB stubbed in for the moment until something better
    explicit IncomingMessage(TraceSink trace_sink)
        : linearizer_(1024 * 1024,
                      trace_sink.Decorate([this](const std::string& msg) {
                        std::ostringstream out;
                        out << "Msg[" << this << "] " << msg;
                        return out.str();
                      })) {}

    void Pull(StatusOrCallback<Optional<Slice>>&& done) {
      linearizer_.Pull(std::forward<StatusOrCallback<Optional<Slice>>>(done));
    }
    void PullAll(StatusOrCallback<std::vector<Slice>>&& done) {
      linearizer_.PullAll(
          std::forward<StatusOrCallback<std::vector<Slice>>>(done));
    }

    void Push(Chunk&& chunk) { linearizer_.Push(std::forward<Chunk>(chunk)); }

    void Close(const Status& status) { linearizer_.Close(status); }

    InternalListNode<IncomingMessage> incoming_link;

   private:
    Linearizer linearizer_;
  };

 public:
  DatagramStream(Router* router, TraceSink sink, NodeId peer,
                 ReliabilityAndOrdering reliability_and_ordering,
                 StreamId stream_id);
  ~DatagramStream();

  DatagramStream(const DatagramStream&) = delete;
  DatagramStream& operator=(const DatagramStream&) = delete;
  DatagramStream(DatagramStream&&) = delete;
  DatagramStream& operator=(DatagramStream&&) = delete;

  void Close(const Status& status, Callback<void> quiesced);
  void Close(Callback<void> quiesced) override {
    Close(Status::Ok(), std::move(quiesced));
  }

  class SendOp final : public Sink<Slice> {
   public:
    SendOp(DatagramStream* stream, uint64_t payload_length);

    void Push(Slice item) override;
    void Close(const Status& status, Callback<void> quiesced) override;
    void Close(Callback<void> quiesced) {
      Close(Status::Ok(), std::move(quiesced));
    }

   private:
    void SetClosed(const Status& status);
    void SendChunk(Chunk chunk);
    void SendError(const Status& status);

    void CompleteReliable(const Status& status, Chunk chunk);
    void CompleteUnreliable(const Status& status);

    void BeginOp() { ++outstanding_ops_; }
    void EndOp() {
      if (0 == --outstanding_ops_) {
        if (!quiesced_.empty())
          quiesced_();
      }
    }

    PacketProtocol::SendCallback MakeAckCallback(const Chunk& chunk);

    class OutstandingOp {
     public:
      OutstandingOp() = delete;
      OutstandingOp(SendOp* op) : op_(op) { op_->BeginOp(); }
      OutstandingOp(const OutstandingOp& other) : op_(other.op_) {
        op_->BeginOp();
      }
      OutstandingOp& operator=(OutstandingOp other) {
        other.Swap(this);
        return *this;
      }
      void Swap(OutstandingOp* other) { std::swap(op_, other->op_); }

      ~OutstandingOp() { op_->EndOp(); }
      SendOp* operator->() const { return op_; }

     private:
      // TODO(ctiller): perhaps consider packing these into one word.
      SendOp* op_;
    };

    enum class State : uint8_t {
      OPEN,
      CLOSED_OK,
      CLOSED_WITH_ERROR,
    };

    DatagramStream* const stream_;
    const TraceSink trace_sink_;
    int outstanding_ops_ = 0;
    const uint64_t payload_length_;
    const uint64_t message_id_;
    const uint8_t message_id_length_;
    State state_ = State::OPEN;
    uint64_t push_offset_ = 0;
    Callback<void> quiesced_;
  };

  class ReceiveOp final : public Source<Slice> {
    friend class DatagramStream;

   public:
    explicit ReceiveOp(DatagramStream* stream);

    void Pull(StatusOrCallback<Optional<Slice>> ready) override;
    void PullAll(StatusOrCallback<std::vector<Slice>> ready) override;
    void Close(const Status& status) override;

   private:
    const TraceSink trace_sink_;
    IncomingMessage* incoming_message_ = nullptr;
    StatusOrCallback<Optional<Slice>> pending_pull_;
    StatusOrCallback<std::vector<Slice>> pending_pull_all_;
    Optional<Status> pending_close_reason_;
    InternalListNode<ReceiveOp> waiting_link_;
  };

  NodeId peer() const { return peer_; }

 private:
  void HandleMessage(SeqNum seq, TimeStamp received, Slice data) override final;
  void SendPacket(SeqNum seq, LazySlice data,
                  Callback<void> done) override final;
  void SendCloseAndFlushQuiesced(const Status& status, int retry_number);
  void FinishClosing();

  void MaybeContinueReceive();

  Timer* const timer_;
  Router* const router_;
  TraceSink const trace_sink_;
  const NodeId peer_;
  const StreamId stream_id_;
  const ReliabilityAndOrdering reliability_and_ordering_;
  uint64_t next_message_id_ = 1;
  uint64_t largest_incoming_message_id_seen_ = 0;
  receive_mode::ParameterizedReceiveMode receive_mode_;
  PacketProtocol packet_protocol_;
  enum class CloseState : uint8_t {
    OPEN,
    LOCAL_CLOSE_REQUESTED,
    REMOTE_CLOSED,
    CLOSING_PROTOCOL,
    CLOSED,
  };
  friend inline std::ostream& operator<<(std::ostream& out, CloseState state) {
    switch (state) {
      case CloseState::OPEN:
        return out << "OPEN";
      case CloseState::LOCAL_CLOSE_REQUESTED:
        return out << "LOCAL_CLOSE_REQUESTED";
      case CloseState::REMOTE_CLOSED:
        return out << "REMOTE_CLOSED";
      case CloseState::CLOSING_PROTOCOL:
        return out << "CLOSING_PROTOCOL";
      case CloseState::CLOSED:
        return out << "CLOSED";
    }
    return out << "UNKNOWN";
  }
  CloseState close_state_ = CloseState::OPEN;
  struct RequestedClose {
    uint64_t last_message_id;
    Status status;
    bool operator==(const RequestedClose& other) const {
      return last_message_id == other.last_message_id &&
             status.code() == other.status.code();
    }
    bool operator!=(const RequestedClose& other) const {
      return !operator==(other);
    }
  };
  Optional<RequestedClose> requested_close_;

  // TODO(ctiller): a custom allocator here would be worthwhile, especially one
  // that could remove allocations for the common case of few entries.
  std::unordered_map<uint64_t, IncomingMessage> messages_;
  InternalList<IncomingMessage, &IncomingMessage::incoming_link>
      unclaimed_messages_;
  InternalList<ReceiveOp, &ReceiveOp::waiting_link_> unclaimed_receives_;

  std::vector<Callback<void>> on_quiesced_;
};

}  // namespace overnet
