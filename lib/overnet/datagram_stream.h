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

namespace overnet {

class MessageFragment {
 public:
  MessageFragment(uint64_t message, Chunk chunk)
      : message_(message), chunk_(chunk) {
    assert(message > 0);
  }

  Slice Write() const;
  static StatusOr<MessageFragment> Parse(Slice incoming);

  uint64_t message() const { return message_; }
  const Chunk& chunk() const { return chunk_; }
  Chunk* mutable_chunk() { return &chunk_; }

 private:
  static constexpr uint8_t kFlagEndOfMessage = 1;
  static constexpr uint8_t kReservedFlags = ~(kFlagEndOfMessage);

  uint64_t message_;
  Chunk chunk_;
};

class DatagramStream : private Router::StreamHandler,
                       private PacketProtocol::PacketSender {
  class IncomingMessage {
   public:
    void Pull(StatusOrCallback<Optional<Slice>>&& done) {
      linearizer_.Pull(std::forward<StatusOrCallback<Optional<Slice>>>(done));
    }

    void Push(Chunk&& chunk, StatusCallback&& done) {
      linearizer_.Push(std::forward<Chunk>(chunk),
                       std::forward<StatusCallback>(done));
    }

    void Close(const Status& status) { linearizer_.Close(status); }

    InternalListNode<IncomingMessage> incoming_link;

   private:
    // TODO(ctiller): 65536 stubbed in for the moment until something better
    Linearizer linearizer_{65536};
  };

 public:
  DatagramStream(Timer* timer, Router* router, NodeId peer,
                 ReliabilityAndOrdering reliability_and_ordering,
                 StreamId stream_id);

  DatagramStream(const DatagramStream&) = delete;
  DatagramStream& operator=(const DatagramStream&) = delete;
  DatagramStream(DatagramStream&&) = delete;
  DatagramStream& operator=(DatagramStream&&) = delete;

  void Close(const Status& status);

  class SendOp final : public Sink<Slice> {
   public:
    SendOp(DatagramStream* stream, uint64_t payload_length);

    void Push(Slice item, StatusCallback sent) override;
    void Close(const Status& status, Callback<void> quiesced) override;

   private:
    void SetClosed(const Status& status);
    void SendChunk(Chunk chunk, StatusCallback done);

    PacketProtocol::SendData ConstructSendData(Chunk chunk,
                                               StatusCallback sent);
    void CompleteReliable(const Status& status, Chunk chunk);
    void CompleteUnreliable(const Status& status);

    class OutstandingPush {
     public:
      OutstandingPush(SendOp* op) : op_(op) { op_->outstanding_pushes_++; }
      OutstandingPush(const OutstandingPush& other) : op_(other.op_) {
        op_->outstanding_pushes_++;
      }
      OutstandingPush& operator=(OutstandingPush other) {
        other.Swap(this);
        return *this;
      }
      void Swap(OutstandingPush* other) { std::swap(op_, other->op_); }
      ~OutstandingPush() {
        if (0 == --op_->outstanding_pushes_ && !op_->quiesced_.empty()) {
          op_->quiesced_();
        }
      }
      SendOp* operator->() const { return op_; }

     private:
      SendOp* op_;
    };

    DatagramStream* const stream_;
    const uint64_t payload_length_;
    const uint64_t message_id_;
    const uint8_t message_id_length_;
    uint64_t push_offset_ = 0;
    int outstanding_pushes_ = 0;
    Callback<void> quiesced_;
    bool closed_ = false;
  };

  class ReceiveOp final : public Source<Slice> {
    friend class DatagramStream;

   public:
    explicit ReceiveOp(DatagramStream* stream);

    void Pull(StatusOrCallback<Optional<Slice>> ready) override;
    void Close(const Status& status) override;

   private:
    IncomingMessage* incoming_message_ = nullptr;
    StatusOrCallback<Optional<Slice>> pending_pull_;
    Optional<Status> pending_close_reason_;
    InternalListNode<ReceiveOp> waiting_link_;
  };

  NodeId peer() const { return peer_; }

 private:
  void HandleMessage(Optional<SeqNum> seq, TimeStamp received, Slice data,
                     StatusCallback done) override final;
  void SendPacket(SeqNum seq, Slice data, StatusCallback done) override final;

  void MaybeContinueReceive();

  Timer* const timer_;
  Router* const router_;
  const NodeId peer_;
  const StreamId stream_id_;
  const ReliabilityAndOrdering reliability_and_ordering_;
  uint64_t next_message_id_ = 1;
  receive_mode::ParameterizedReceiveMode receive_mode_;
  PacketProtocol packet_protocol_;

  // TODO(ctiller): a custom allocator here would be worthwhile, especially one
  // that could remove allocations for the common case of few entries.
  std::unordered_map<uint64_t, IncomingMessage> messages_;
  InternalList<IncomingMessage, &IncomingMessage::incoming_link>
      unclaimed_messages_;
  InternalList<ReceiveOp, &ReceiveOp::waiting_link_> unclaimed_receives_;
};

}  // namespace overnet
