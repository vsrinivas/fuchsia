// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "datagram_stream.h"

namespace overnet {

////////////////////////////////////////////////////////////////////////////////
// MessageFragment

Slice MessageFragment::Write() const {
  const auto message_length = varint::WireSizeFor(message_);
  const auto chunk_offset_length = varint::WireSizeFor(chunk_.offset);
  const auto flags = (chunk_.end_of_message ? kFlagEndOfMessage : 0);
  return chunk_.slice.WithPrefix(
      message_length + chunk_offset_length + 1,
      [this, message_length, chunk_offset_length, flags](uint8_t* const bytes) {
        uint8_t* p = bytes;
        *p++ = flags;
        p = varint::Write(message_, message_length, p);
        p = varint::Write(chunk_.offset, chunk_offset_length, p);
        assert(p == bytes + message_length + chunk_offset_length + 1);
      });
}

StatusOr<MessageFragment> MessageFragment::Parse(Slice slice) {
  const uint8_t* p = slice.begin();
  const uint8_t* const end = slice.end();
  uint64_t message;
  uint64_t chunk_offset;
  if (p == end) {
    return StatusOr<MessageFragment>(
        StatusCode::INVALID_ARGUMENT,
        "Failed to read flags from message fragment");
  }
  const uint8_t flags = *p++;
  if (flags & kReservedFlags) {
    return StatusOr<MessageFragment>(
        StatusCode::INVALID_ARGUMENT,
        "Reserved flags set on message fragment flags field");
  }
  if (!varint::Read(&p, end, &message)) {
    return StatusOr<MessageFragment>(
        StatusCode::INVALID_ARGUMENT,
        "Failed to read message id from message fragment");
  }
  if (message == 0) {
    return StatusOr<MessageFragment>(StatusCode::INVALID_ARGUMENT,
                                     "Message id 0 is invalid");
  }
  if (!varint::Read(&p, end, &chunk_offset)) {
    return StatusOr<MessageFragment>(
        StatusCode::INVALID_ARGUMENT,
        "Failed to read chunk offset from message fragment");
  }
  return MessageFragment{message,
                         Chunk{chunk_offset, (flags & kFlagEndOfMessage) != 0,
                               slice.FromPointer(p)}};
}

////////////////////////////////////////////////////////////////////////////////
// DatagramStream proper

DatagramStream::DatagramStream(Timer* timer, Router* router, NodeId peer,
                               ReliabilityAndOrdering reliability_and_ordering,
                               StreamId stream_id)
    : timer_(timer),
      router_(router),
      peer_(peer),
      stream_id_(stream_id),
      reliability_and_ordering_(reliability_and_ordering),
      receive_mode_(reliability_and_ordering),
      // TODO(ctiller): What should mss be? Hardcoding to 65536 for now.
      packet_protocol_(timer, this, 65536) {
  if (router_->RegisterStream(peer_, stream_id_, this).is_error()) {
    abort();
  }
}

void DatagramStream::Close(const Status& status) {
  receive_mode_.Close(status);
}

void DatagramStream::HandleMessage(Optional<SeqNum> seq, TimeStamp received,
                                   Slice data, StatusCallback done) {
  if (seq.has_value()) {
    auto pkt_status =
        packet_protocol_.Process(received, seq.value(), std::move(data));
    if (pkt_status.is_error()) {
      done(pkt_status.AsStatus());
      return;
    }
    auto payload = std::move(pkt_status.value());
    if (!payload || !payload->length()) {
      done(Status::Ok());
      return;
    }
    auto msg_status = MessageFragment::Parse(std::move(*payload));
    if (msg_status.is_error()) {
      done(msg_status.AsStatus());
      return;
    }
    auto msg = std::move(msg_status.value());
    auto it = messages_.find(msg.message());
    if (it == messages_.end()) {
      it = messages_
               .emplace(std::piecewise_construct,
                        std::forward_as_tuple(msg.message()),
                        std::forward_as_tuple())
               .first;
      receive_mode_.Begin(
          msg.message(), [this, msg = std::move(msg), done = std::move(done)](
                             const Status& status) mutable {
            if (status.is_error()) {
              done(status);
              return;
            }
            auto it = messages_.find(msg.message());
            if (it == messages_.end()) {
              done(Status::Cancelled());
              return;
            }
            it->second.Push(std::move(*msg.mutable_chunk()), std::move(done));
            unclaimed_messages_.PushBack(&it->second);
            MaybeContinueReceive();
          });
    } else {
      it->second.Push(std::move(*msg.mutable_chunk()), std::move(done));
    }
  } else {
    // TODO(ctiller): support control messages
    abort();
  }
}

void DatagramStream::MaybeContinueReceive() {
  if (unclaimed_messages_.Empty()) return;
  if (unclaimed_receives_.Empty()) return;

  auto incoming_message = unclaimed_messages_.PopFront();
  auto receive_op = unclaimed_receives_.PopFront();

  receive_op->incoming_message_ = incoming_message;
  if (receive_op->pending_close_reason_) {
    incoming_message->Close(*receive_op->pending_close_reason_);
  } else if (!receive_op->pending_pull_.empty()) {
    incoming_message->Pull(std::move(receive_op->pending_pull_));
  }
}

void DatagramStream::SendPacket(SeqNum seq, Slice data, StatusCallback done) {
  router_->Forward(Message{
      std::move(RoutableMessage(router_->node_id(), false, std::move(data))
                    .AddDestination(peer_, stream_id_, seq)),
      timer_->Now(), std::move(done)});
}

////////////////////////////////////////////////////////////////////////////////
// SendOp

DatagramStream::SendOp::SendOp(DatagramStream* stream, uint64_t payload_length)
    : stream_(stream),
      payload_length_(payload_length),
      message_id_(stream_->next_message_id_++),
      message_id_length_(varint::WireSizeFor(message_id_)) {}

void DatagramStream::SendOp::SetClosed(const Status& status) {
  if (status.is_ok() && payload_length_ != push_offset_) {
    SetClosed(Status(StatusCode::INVALID_ARGUMENT,
                     "Insufficient bytes for message presented"));
    return;
  }
  closed_ = true;
}

void DatagramStream::SendOp::Close(const Status& status,
                                   Callback<void> quiesced) {
  SetClosed(status);
  if (outstanding_pushes_ == 0) {
    quiesced();
  } else {
    quiesced_ = std::move(quiesced);
  }
}

void DatagramStream::SendOp::Push(Slice item, StatusCallback done) {
  uint64_t end_byte = push_offset_ + item.length();
  if (end_byte > payload_length_) {
    SetClosed(Status(StatusCode::INVALID_ARGUMENT,
                     "Exceeded message payload length"));
    return;
  }
  Chunk chunk{push_offset_, end_byte == payload_length_, std::move(item)};
  push_offset_ += item.length();
  SendChunk(std::move(chunk), std::move(done));
}

void DatagramStream::SendOp::SendChunk(Chunk chunk, StatusCallback sent) {
  stream_->packet_protocol_.Send([this, chunk = std::move(chunk),
                                  sent = std::move(sent)](
                                     uint64_t desired_prefix_hint,
                                     uint64_t max_len) mutable {
    assert(max_len > message_id_length_ + varint::WireSizeFor(chunk.offset));
    uint64_t take_len =
        varint::MaximumLengthWithPrefix(max_len - message_id_length_);
    if (take_len >= chunk.slice.length()) {
      return ConstructSendData(std::move(chunk), std::move(sent));
    } else {
      Chunk first = chunk.TakeUntilSliceOffset(chunk.slice.length() - take_len);
      return ConstructSendData(
          first,
          StatusCallback(ALLOCATED_CALLBACK,
                         [sent = std::move(sent), chunk = std::move(chunk),
                          self = OutstandingPush(this)](
                             const Status& first_send_status) mutable {
                           if (first_send_status.is_error()) {
                             sent(first_send_status);
                             return;
                           }
                           self->SendChunk(std::move(chunk), std::move(sent));
                         }));
    }
  });
}

PacketProtocol::SendData DatagramStream::SendOp::ConstructSendData(
    Chunk chunk, StatusCallback sent) {
  switch (stream_->reliability_and_ordering_) {
    case ReliabilityAndOrdering::ReliableOrdered:
    case ReliabilityAndOrdering::ReliableUnordered:
      return PacketProtocol::SendData{
          MessageFragment(message_id_, chunk).Write(), std::move(sent),
          [chunk, self = OutstandingPush(this)](const Status& status) mutable {
            self->CompleteReliable(status, std::move(chunk));
          }};
    case ReliabilityAndOrdering::UnreliableOrdered:
    case ReliabilityAndOrdering::UnreliableUnordered:
      return PacketProtocol::SendData{
          MessageFragment(message_id_, std::move(chunk)).Write(),
          std::move(sent),
          [self = OutstandingPush(this)](const Status& status) {
            self->CompleteUnreliable(status);
          }};
    case ReliabilityAndOrdering::TailReliable:
      return PacketProtocol::SendData{
          MessageFragment(message_id_, chunk).Write(), std::move(sent),
          [chunk, self = OutstandingPush(this)](const Status& status) mutable {
            if (self->message_id_ + 1 == self->stream_->next_message_id_) {
              self->CompleteReliable(status, std::move(chunk));
            } else {
              self->CompleteUnreliable(status);
            }
          }};
  }
}

void DatagramStream::SendOp::CompleteReliable(const Status& status,
                                              Chunk chunk) {
  if (closed_) return;
  if (status.is_ok()) return;
  // Send failed, still open: retry.
  SendChunk(std::move(chunk), StatusCallback::Ignored());
}

void DatagramStream::SendOp::CompleteUnreliable(const Status& status) {
  if (status.is_error()) {
    SetClosed(status);
  }
}

///////////////////////////////////////////////////////////////////////////////
// ReceiveOp

DatagramStream::ReceiveOp::ReceiveOp(DatagramStream* stream) {
  stream->unclaimed_receives_.PushBack(this);
  stream->MaybeContinueReceive();
}

void DatagramStream::ReceiveOp::Pull(StatusOrCallback<Optional<Slice>> ready) {
  if (incoming_message_ == nullptr) {
    if (pending_close_reason_) {
      ready(*pending_close_reason_);
    } else {
      pending_pull_ = std::move(ready);
    }
  } else {
    incoming_message_->Pull(std::move(ready));
  }
}

void DatagramStream::ReceiveOp::Close(const Status& status) {
  if (incoming_message_ == nullptr) {
    pending_close_reason_ = status;
    if (!pending_pull_.empty()) {
      pending_pull_(status);
    }
  } else {
    incoming_message_->Close(status);
  }
}

}  // namespace overnet
