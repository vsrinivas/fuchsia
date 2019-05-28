// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/datagram_stream/linearizer.h"
#include "src/connectivity/overnet/lib/datagram_stream/receive_mode.h"
#include "src/connectivity/overnet/lib/datagram_stream/stream_state.h"
#include "src/connectivity/overnet/lib/environment/timer.h"
#include "src/connectivity/overnet/lib/environment/trace.h"
#include "src/connectivity/overnet/lib/labels/seq_num.h"
#include "src/connectivity/overnet/lib/packet_protocol/packet_protocol.h"
#include "src/connectivity/overnet/lib/routing/router.h"
#include "src/connectivity/overnet/lib/stats/stream.h"
#include "src/connectivity/overnet/lib/vocabulary/internal_list.h"
#include "src/connectivity/overnet/lib/vocabulary/slice.h"

namespace overnet {

class MessageFragment {
 public:
  enum class Type : uint8_t { Chunk = 0, MessageCancel = 1, StreamEnd = 2 };

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
      case Type::MessageCancel:
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
        case Type::MessageCancel:
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
      case Type::MessageCancel:
      case Type::StreamEnd:
        payload_.status.~Status();
        break;
    }
  }

  static MessageFragment Abort(uint64_t message_id, Status status) {
    return MessageFragment(message_id, Type::MessageCancel, std::move(status));
  }

  static MessageFragment EndOfStream(uint64_t last_message_id, Status status) {
    return MessageFragment(last_message_id, Type::StreamEnd, std::move(status));
  }

  Slice Write(Border desired_border) const;
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
    assert(type_ == Type::MessageCancel || type_ == Type::StreamEnd);
    return payload_.status;
  }

 private:
  static constexpr uint8_t kFlagEndOfMessage = 0x80;
  static constexpr uint8_t kFlagTypeMask = 0x0f;
  static constexpr uint8_t kReservedFlags =
      static_cast<uint8_t>(~(kFlagTypeMask | kFlagEndOfMessage));

  MessageFragment(uint64_t message, Type type, Status status)
      : message_(message), type_(type) {
    assert(type == Type::MessageCancel || type == Type::StreamEnd);
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
                       private StreamStateListener,
                       private PacketProtocol::PacketSender {
  class IncomingMessage {
   public:
    inline static constexpr auto kModule =
        Module::DATAGRAM_STREAM_INCOMING_MESSAGE;

    IncomingMessage(DatagramStream* stream, uint64_t msg_id)
        : protocol_(&stream->packet_protocol_),
          // TODO(ctiller): What should the bound be here? 4*mss is a guess,
          // nothing more.
          linearizer_(std::max(uint64_t(4 * protocol_->maximum_send_size()),
                               protocol_->bdp_estimate()),
                      &stream->stream_stats_),
          msg_id_(msg_id) {}

    void Pull(StatusOrCallback<Optional<Slice>>&& done) {
      ScopedModule<IncomingMessage> in_im(this);
      linearizer_.Pull(std::forward<StatusOrCallback<Optional<Slice>>>(done));
    }
    void PullAll(StatusOrCallback<Optional<std::vector<Slice>>>&& done) {
      ScopedModule<IncomingMessage> in_im(this);
      linearizer_.PullAll(
          std::forward<StatusOrCallback<Optional<std::vector<Slice>>>>(done));
    }

    [[nodiscard]] bool Push(Chunk&& chunk) {
      ScopedModule<IncomingMessage> in_im(this);
      linearizer_.UpdateMaxBuffer(protocol_->bdp_estimate());
      return linearizer_.Push(std::forward<Chunk>(chunk));
    }

    Status Close(const Status& status) {
      ScopedModule<IncomingMessage> in_im(this);
      return linearizer_.Close(status);
    }

    bool IsComplete() const { return linearizer_.IsComplete(); }

    uint64_t msg_id() const { return msg_id_; }

    InternalListNode<IncomingMessage> incoming_link;

   private:
    PacketProtocol* const protocol_;
    Linearizer linearizer_;
    const uint64_t msg_id_;
  };

  struct SendState {
    enum State : uint8_t {
      OPEN,
      CLOSED_OK,
      CLOSED_WITH_ERROR,
    };
    State state = State::OPEN;
    int refs = 0;
  };
  using SendStateMap = std::unordered_map<uint64_t, SendState>;
  using SendStateIt = SendStateMap::iterator;

  // Keeps a stream from quiescing while the ref is not abandoned.
  class StreamRef {
   public:
    StreamRef(DatagramStream* stream) : stream_(stream) {
      assert(stream_);
      stream_->stream_state_.BeginOp();
    }
    StreamRef(const StreamRef& other) : stream_(other.stream_) {
      stream_->stream_state_.BeginOp();
    }
    StreamRef& operator=(const StreamRef& other) {
      StreamRef copy(other);
      Swap(&copy);
      return *this;
    }
    StreamRef(StreamRef&& other) : stream_(other.stream_) {
      other.stream_ = nullptr;
    }
    StreamRef& operator=(StreamRef&& other) {
      std::swap(stream_, other.stream_);
      return *this;
    }
    void Swap(StreamRef* other) { std::swap(stream_, other->stream_); }
    ~StreamRef() { Abandon(); }
    void Abandon() {
      if (auto* stream = stream_) {
        stream_ = nullptr;
        stream->stream_state_.EndOp();
      }
    }
    DatagramStream* get() const {
      assert(stream_);
      return stream_;
    }
    DatagramStream* operator->() const {
      assert(stream_);
      return stream_;
    }
    bool has_value() const { return stream_ != nullptr; }

   private:
    DatagramStream* stream_;
  };

  class SendStateRef {
   public:
    SendStateRef() = delete;
    SendStateRef(DatagramStream* stream, SendStateIt op)
        : stream_(stream), op_(op) {
      stream_->stream_state_.BeginSend();
      op_->second.refs++;
    }
    SendStateRef(const SendStateRef& other)
        : stream_(other.stream_), op_(other.op_) {
      stream_->stream_state_.BeginSend();
      op_->second.refs++;
    }
    SendStateRef& operator=(const SendStateRef& other) {
      SendStateRef copy(other);
      Swap(&copy);
      return *this;
    }
    SendStateRef(SendStateRef&& other)
        : stream_(other.stream_), op_(other.op_) {
      other.stream_ = nullptr;
    }
    SendStateRef& operator=(SendStateRef&& other) {
      SendStateRef moved(std::move(other));
      Swap(&moved);
      return *this;
    }

    void Swap(SendStateRef* other) {
      std::swap(op_, other->op_);
      std::swap(stream_, other->stream_);
    }

    ~SendStateRef() {
      if (stream_ != nullptr) {
        if (0 == --op_->second.refs) {
          stream_->message_state_.erase(op_);
        }
        stream_->stream_state_.EndSend();
      }
    }

    void SetClosed(const Status& status);

    DatagramStream* stream() const {
      assert(stream_ != nullptr);
      return stream_;
    }
    SendState::State state() const {
      assert(stream_ != nullptr);
      return op_->second.state;
    }
    void set_state(SendState::State state) {
      assert(stream_ != nullptr);
      op_->second.state = state;
    }
    uint64_t message_id() const {
      assert(stream_ != nullptr);
      return op_->first;
    }

   private:
    DatagramStream* stream_;
    SendStateIt op_;
  };

 public:
  inline static constexpr auto kModule = Module::DATAGRAM_STREAM;

  DatagramStream(Router* router, NodeId peer,
                 fuchsia::overnet::protocol::ReliabilityAndOrdering
                     reliability_and_ordering,
                 StreamId stream_id);
  ~DatagramStream();

  DatagramStream(const DatagramStream&) = delete;
  DatagramStream& operator=(const DatagramStream&) = delete;
  DatagramStream(DatagramStream&&) = delete;
  DatagramStream& operator=(DatagramStream&&) = delete;

  virtual void Close(const Status& status, Callback<void> quiesced);
  void Close(Callback<void> quiesced) {
    Close(Status::Ok(), std::move(quiesced));
  }
  void RouterClose(Callback<void> quiesced) override final {
    Close(Status::Cancelled(), std::move(quiesced));
  }

  class SendOp final : private SendStateRef {
   public:
    inline static constexpr auto kModule = Module::DATAGRAM_STREAM_SEND_OP;

    SendOp(DatagramStream* stream, uint64_t payload_length);
    ~SendOp();

    void Push(Slice item, Callback<void> started);
    void Close(const Status& status);

   private:
    const uint64_t payload_length_;
    uint64_t push_offset_ = 0;
  };

  class ReceiveOp final {
    friend class DatagramStream;

   public:
    inline static constexpr auto kModule = Module::DATAGRAM_STREAM_RECV_OP;

    explicit ReceiveOp(DatagramStream* stream);
    ~ReceiveOp() {
      if (!closed_) {
        Close(incoming_message_ && incoming_message_->IsComplete()
                  ? Status::Ok()
                  : Status::Cancelled());
      }
      assert(closed_);
    }
    ReceiveOp(const ReceiveOp& rhs) = delete;
    ReceiveOp& operator=(const ReceiveOp& rhs) = delete;

    void Pull(StatusOrCallback<Optional<Slice>> ready);
    void PullAll(StatusOrCallback<Optional<std::vector<Slice>>> ready);
    void Close(const Status& status);

   private:
    StreamRef stream_;
    IncomingMessage* incoming_message_ = nullptr;
    bool closed_ = false;
    StatusOrCallback<Optional<Slice>> pending_pull_;
    StatusOrCallback<Optional<std::vector<Slice>>> pending_pull_all_;
    InternalListNode<ReceiveOp> waiting_link_;
  };

  NodeId peer() const { return peer_; }

  const LinkStats* link_stats() const { return packet_protocol_.stats(); }
  const StreamStats* stream_stats() const { return &stream_stats_; }

  bool IsClosedForSending() const { return stream_state_.IsClosedForSending(); }

 protected:
  // Must be called by derived classes, after construction and before any other
  // methods.
  void Register();

 private:
  void HandleMessage(SeqNum seq, TimeStamp received, Slice data) override final;
  void SendPacket(SeqNum seq, LazySlice packet) override final;
  void NoConnectivity() override final;

  void MaybeContinueReceive();

  void SendChunk(SendStateRef state, Chunk chunk, Callback<void> started);
  void SendNextChunk();
  void SendMessageError(SendStateRef state, const Status& status);
  void CompleteReliable(const Status& status, SendStateRef state, Chunk chunk);
  void CompleteUnreliable(const Status& status, SendStateRef state);
  std::string PendingSendString();

  // StreamStateListener
  void SendClose() override final;
  void StreamClosed() override final;
  void StopReading(const Status& status) override final;

  Timer* const timer_;
  Router* const router_;
  const NodeId peer_;
  const StreamId stream_id_;
  const fuchsia::overnet::protocol::ReliabilityAndOrdering
      reliability_and_ordering_;
  uint64_t next_message_id_ = 1;
  uint64_t largest_incoming_message_id_seen_ = 0;
  receive_mode::ParameterizedReceiveMode receive_mode_;
  PacketProtocol packet_protocol_;
  StreamStats stream_stats_;
  StreamState stream_state_;
  StreamRef close_ref_{this};  // Keep stream alive until closed

  struct ChunkAndState {
    Chunk chunk;
    SendStateRef state;
  };

  struct PendingSend {
    ChunkAndState what;
    Callback<void> started;
  };
  std::vector<PendingSend> pending_send_;
  bool sending_ = false;

  SendStateMap message_state_;

  // TODO(ctiller): a custom allocator here would be worthwhile, especially one
  // that could remove allocations for the common case of few entries.
  std::unordered_map<uint64_t, IncomingMessage> messages_;
  InternalList<IncomingMessage, &IncomingMessage::incoming_link>
      unclaimed_messages_;
  InternalList<ReceiveOp, &ReceiveOp::waiting_link_> unclaimed_receives_;
};

}  // namespace overnet
