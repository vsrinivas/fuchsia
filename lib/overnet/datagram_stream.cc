// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "datagram_stream.h"
#include <sstream>

namespace overnet {

////////////////////////////////////////////////////////////////////////////////
// MessageFragment

Slice MessageFragment::Write(uint64_t desired_prefix) const {
  const auto message_length = varint::WireSizeFor(message_);
  uint8_t flags = static_cast<uint8_t>(type_);
  assert((flags & kFlagTypeMask) == flags);
  switch (type_) {
    case Type::Chunk: {
      const Chunk& chunk = this->chunk();
      if (chunk.end_of_message) {
        flags |= kFlagEndOfMessage;
      }
      const auto chunk_offset_length = varint::WireSizeFor(chunk.offset);
      return chunk.slice.WithPrefix(
          message_length + chunk_offset_length + 1, [&](uint8_t* const bytes) {
            uint8_t* p = bytes;
            *p++ = flags;
            p = varint::Write(message_, message_length, p);
            p = varint::Write(chunk.offset, chunk_offset_length, p);
            assert(p == bytes + message_length + chunk_offset_length + 1);
          });
    }
    case Type::MessageAbort:
    case Type::StreamEnd: {
      const Status& status = this->status();
      const auto& reason = status.reason();
      const auto reason_length_length = varint::WireSizeFor(reason.length());
      const auto frame_length =
          1 + message_length + 1 + reason_length_length + reason.length();
      return Slice::WithInitializerAndPrefix(
          frame_length, desired_prefix, [&](uint8_t* bytes) {
            uint8_t* p = bytes;
            *p++ = flags;
            p = varint::Write(message_, message_length, p);
            *p++ = static_cast<uint8_t>(status.code());
            p = varint::Write(reason.length(), reason_length_length, p);
            assert(p + reason.length() == bytes + frame_length);
            memcpy(p, reason.data(), reason.length());
          });
    }
  }
  abort();
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
  const Type type = static_cast<Type>(flags & kFlagTypeMask);
  switch (type) {
    case Type::Chunk:
      if (!varint::Read(&p, end, &chunk_offset)) {
        return StatusOr<MessageFragment>(
            StatusCode::INVALID_ARGUMENT,
            "Failed to read chunk offset from message fragment");
      }
      return MessageFragment{
          message, Chunk{chunk_offset, (flags & kFlagEndOfMessage) != 0,
                         slice.FromPointer(p)}};
    case Type::MessageAbort:
    case Type::StreamEnd: {
      if (p == end) {
        return StatusOr<MessageFragment>(
            StatusCode::INVALID_ARGUMENT,
            "Failed to read status code from message fragment");
      }
      const uint8_t code = *p++;
      uint64_t reason_length;
      if (!varint::Read(&p, end, &reason_length)) {
        return StatusOr<MessageFragment>(
            StatusCode::INVALID_ARGUMENT,
            "Failed to read status reason length from message fragment");
      }
      return MessageFragment(message, type,
                             Status(static_cast<StatusCode>(code),
                                    std::string(p, p + reason_length)));
    } break;
    default:
      return StatusOr<MessageFragment>(StatusCode::INVALID_ARGUMENT,
                                       "Unknown message fragment type");
  }
}

////////////////////////////////////////////////////////////////////////////////
// DatagramStream proper

DatagramStream::DatagramStream(Router* router, TraceSink trace_sink,
                               NodeId peer,
                               ReliabilityAndOrdering reliability_and_ordering,
                               StreamId stream_id)
    : timer_(router->timer()),
      router_(router),
      trace_sink_(trace_sink.Decorate([this, peer, stream_id](std::string msg) {
        std::ostringstream out;
        out << "DGS[" << this << ";peer=" << peer << ";stream=" << stream_id
            << "] " << msg;
        return out.str();
      })),
      peer_(peer),
      stream_id_(stream_id),
      reliability_and_ordering_(reliability_and_ordering),
      receive_mode_(reliability_and_ordering),
      // TODO(ctiller): What should mss be? Hardcoding to 65536 for now.
      packet_protocol_(timer_, this, trace_sink_, 65536) {
  if (router_->RegisterStream(peer_, stream_id_, this).is_error()) {
    abort();
  }
}

DatagramStream::~DatagramStream() {
  assert(close_state_ == CloseState::CLOSED);
}

void DatagramStream::Close(const Status& status, Callback<void> quiesced) {
  OVERNET_TRACE(DEBUG, trace_sink_) << "Close: state=" << close_state_;

  switch (close_state_) {
    case CloseState::CLOSED:
      return;
    case CloseState::CLOSING_PROTOCOL:
    case CloseState::LOCAL_CLOSE_REQUESTED:
      on_quiesced_.emplace_back(std::move(quiesced));
      return;
    case CloseState::REMOTE_CLOSED:
      on_quiesced_.emplace_back(std::move(quiesced));
      FinishClosing();
      return;
    case CloseState::OPEN:
      on_quiesced_.emplace_back(std::move(quiesced));
      receive_mode_.Close(status);
      close_state_ = CloseState::LOCAL_CLOSE_REQUESTED;
      SendCloseAndFlushQuiesced(status, 0);
      return;
  }
}

void DatagramStream::SendCloseAndFlushQuiesced(const Status& status,
                                               int retry_number) {
  assert(close_state_ == CloseState::LOCAL_CLOSE_REQUESTED);
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "SendClose: status=" << status << " retry=" << retry_number
      << " state=" << close_state_;
  packet_protocol_.Send(
      [this, status](auto args) {
        return MessageFragment::EndOfStream(next_message_id_, status)
            .Write(args.desired_prefix);
      },
      [status, this, retry_number](const Status& send_status) mutable {
        OVERNET_TRACE(DEBUG, trace_sink_)
            << "SendClose ACK status=" << send_status
            << " retry=" << retry_number;
        switch (close_state_) {
          case CloseState::OPEN:
          case CloseState::REMOTE_CLOSED:
            assert(false);
            break;
          case CloseState::CLOSED:
          case CloseState::CLOSING_PROTOCOL:
            break;
          case CloseState::LOCAL_CLOSE_REQUESTED:
            if (send_status.code() == StatusCode::UNAVAILABLE) {
              SendCloseAndFlushQuiesced(status, retry_number + 1);
            } else {
              FinishClosing();
            }
            break;
        }
      });
}

void DatagramStream::FinishClosing() {
  assert(close_state_ == CloseState::LOCAL_CLOSE_REQUESTED ||
         close_state_ == CloseState::REMOTE_CLOSED);
  close_state_ = CloseState::CLOSING_PROTOCOL;
  packet_protocol_.Close([this]() {
    close_state_ = CloseState::CLOSED;

    auto unregister_status = router_->UnregisterStream(peer_, stream_id_, this);
    assert(unregister_status.is_ok());

    std::vector<Callback<void>> on_quiesced;
    on_quiesced.swap(on_quiesced_);
    on_quiesced.clear();
  });
}

void DatagramStream::HandleMessage(SeqNum seq, TimeStamp received, Slice data) {
  switch (close_state_) {
    // In these states we process messages:
    case CloseState::OPEN:
    case CloseState::LOCAL_CLOSE_REQUESTED:
    case CloseState::REMOTE_CLOSED:
      break;
    // In these states we do not:
    case CloseState::CLOSING_PROTOCOL:
    case CloseState::CLOSED:
      return;
  }

  auto pkt_status = packet_protocol_.Process(received, seq, std::move(data));
  if (pkt_status.status.is_error()) {
    OVERNET_TRACE(WARNING, trace_sink_)
        << "Failed to process packet: " << pkt_status.status.AsStatus();
    return;
  }
  auto payload = std::move(pkt_status.status.value());
  if (!payload || !payload->length()) {
    return;
  }
  OVERNET_TRACE(DEBUG, trace_sink_) << "Process payload " << payload;
  auto msg_status = MessageFragment::Parse(std::move(*payload));
  if (msg_status.is_error()) {
    OVERNET_TRACE(WARNING, trace_sink_)
        << "Failed to parse message: " << msg_status.AsStatus();
    return;
  }
  auto msg = std::move(*msg_status.get());
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "Payload type=" << static_cast<int>(msg.type())
      << " msg=" << msg.message();
  switch (msg.type()) {
    case MessageFragment::Type::Chunk: {
      OVERNET_TRACE(DEBUG, trace_sink_)
          << "chunk offset=" << msg.chunk().offset
          << " length=" << msg.chunk().slice.length()
          << " end-of-message=" << msg.chunk().end_of_message;
      // Got a chunk of data: add it to the relevant incoming message.
      largest_incoming_message_id_seen_ =
          std::max(largest_incoming_message_id_seen_, msg.message());
      auto it = messages_.find(msg.message());
      if (it == messages_.end()) {
        it = messages_
                 .emplace(std::piecewise_construct,
                          std::forward_as_tuple(msg.message()),
                          std::forward_as_tuple(trace_sink_))
                 .first;
        receive_mode_.Begin(msg.message(), [this, msg = std::move(msg)](
                                               const Status& status) mutable {
          if (status.is_error()) {
            OVERNET_TRACE(WARNING, trace_sink_) << "Receive failed: " << status;
            return;
          }
          auto it = messages_.find(msg.message());
          if (it == messages_.end()) {
            return;
          }
          it->second.Push(std::move(*msg.mutable_chunk()));
          unclaimed_messages_.PushBack(&it->second);
          MaybeContinueReceive();
        });
      } else {
        it->second.Push(std::move(*msg.mutable_chunk()));
      }
    } break;
    case MessageFragment::Type::MessageAbort: {
      // Aborting a message: this is like a close to the incoming message.
      largest_incoming_message_id_seen_ =
          std::max(largest_incoming_message_id_seen_, msg.message());
      auto it = messages_.find(msg.message());
      if (it == messages_.end()) {
        it = messages_
                 .emplace(std::piecewise_construct,
                          std::forward_as_tuple(msg.message()),
                          std::forward_as_tuple(trace_sink_))
                 .first;
      }
      it->second.Close(msg.status());
    } break;
    case MessageFragment::Type::StreamEnd:
      // TODO(ctiller): handle case of ok termination with outstanding
      // messages.
      RequestedClose requested_close{msg.message(), msg.status()};
      OVERNET_TRACE(DEBUG, trace_sink_)
          << "peer requests close with status " << msg.status();
      if (requested_close_.has_value()) {
        if (*requested_close_ != requested_close) {
          OVERNET_TRACE(WARNING, trace_sink_)
              << "Non-duplicate last message id received: previously got "
              << requested_close_->last_message_id << " with status "
              << requested_close_->status << " now have " << msg.message()
              << " with status " << msg.status();
        }
        return;
      }
      requested_close_ = requested_close;
      auto enact_remote_close = [this](const Status& status) {
        packet_protocol_.RequestSendAck();
        switch (close_state_) {
          case CloseState::OPEN:
            close_state_ = CloseState::REMOTE_CLOSED;
            receive_mode_.Close(status);
            break;
          case CloseState::REMOTE_CLOSED:
            assert(false);
            break;
          case CloseState::LOCAL_CLOSE_REQUESTED:
            FinishClosing();
            break;
          case CloseState::CLOSED:
          case CloseState::CLOSING_PROTOCOL:
            break;
        }
      };
      if (requested_close_->status.is_error()) {
        enact_remote_close(requested_close_->status);
      } else {
        receive_mode_.Begin(msg.message(), std::move(enact_remote_close));
      }
      break;
  }
}

void DatagramStream::MaybeContinueReceive() {
  if (unclaimed_messages_.Empty())
    return;
  if (unclaimed_receives_.Empty())
    return;

  auto incoming_message = unclaimed_messages_.PopFront();
  auto receive_op = unclaimed_receives_.PopFront();

  receive_op->incoming_message_ = incoming_message;
  if (receive_op->pending_close_reason_) {
    incoming_message->Close(*receive_op->pending_close_reason_);
  } else if (!receive_op->pending_pull_.empty()) {
    incoming_message->Pull(std::move(receive_op->pending_pull_));
  } else if (!receive_op->pending_pull_all_.empty()) {
    incoming_message->PullAll(std::move(receive_op->pending_pull_all_));
  }
}

void DatagramStream::SendPacket(SeqNum seq, LazySlice data,
                                Callback<void> done) {
  router_->Forward(
      Message{std::move(RoutableMessage(router_->node_id())
                            .AddDestination(peer_, stream_id_, seq)),
              std::move(data), timer_->Now()});
}

////////////////////////////////////////////////////////////////////////////////
// SendOp

DatagramStream::SendOp::SendOp(DatagramStream* stream, uint64_t payload_length)
    : stream_(stream),
      trace_sink_(stream->trace_sink_.Decorate([this](const std::string& msg) {
        std::ostringstream out;
        out << "SendOp[" << this << "] " << msg;
        return out.str();
      })),
      payload_length_(payload_length),
      message_id_(stream_->next_message_id_++),
      message_id_length_(varint::WireSizeFor(message_id_)) {}

void DatagramStream::SendOp::SetClosed(const Status& status) {
  if (state_ != State::OPEN) {
    return;
  }
  if (status.is_ok() && payload_length_ != push_offset_) {
    std::ostringstream out;
    out << "Insufficient bytes for message presented: expected "
        << payload_length_ << " but got " << push_offset_;
    SetClosed(Status(StatusCode::INVALID_ARGUMENT, out.str()));
    return;
  }
  OVERNET_TRACE(DEBUG, trace_sink_) << "SET CLOSED: " << status;
  if (status.is_error()) {
    state_ = State::CLOSED_WITH_ERROR;
    SendError(status);
  } else {
    state_ = State::CLOSED_OK;
  }
}

void DatagramStream::SendOp::SendError(const overnet::Status& status) {
  stream_->packet_protocol_.Send(
      [this, status](auto arg) {
        return MessageFragment::Abort(message_id_, status)
            .Write(arg.desired_prefix);
      },
      [self = OutstandingOp(this), status](const Status& send_status) {
        if (send_status.code() == StatusCode::UNAVAILABLE) {
          self->SendError(status);
        }
      });
}

void DatagramStream::SendOp::Close(const Status& status,
                                   Callback<void> quiesced) {
  SetClosed(status);
  if (outstanding_ops_ == 0) {
    quiesced();
  } else {
    quiesced_ = std::move(quiesced);
  }
}

void DatagramStream::SendOp::Push(Slice item) {
  assert(state_ == State::OPEN);
  if (state_ != State::OPEN) {
    return;
  }
  const auto chunk_start = push_offset_;
  const auto chunk_length = item.length();
  const auto end_byte = chunk_start + chunk_length;
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "Push: chunk_start=" << chunk_start << " chunk_length=" << chunk_length
      << " end_byte=" << end_byte << " payload_length=" << payload_length_;
  if (end_byte > payload_length_) {
    SetClosed(Status(StatusCode::INVALID_ARGUMENT,
                     "Exceeded message payload length"));
    return;
  }
  push_offset_ += chunk_length;
  Chunk chunk{chunk_start, end_byte == payload_length_, std::move(item)};
  SendChunk(std::move(chunk));
}

void DatagramStream::SendOp::SendChunk(Chunk chunk) {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "SendChunk: ofs=" << chunk.offset << " len=" << chunk.slice.length();
  auto on_ack = MakeAckCallback(chunk);
  stream_->packet_protocol_.Send(
      [self = OutstandingOp(this),
       chunk = std::move(chunk)](auto args) mutable {
        OVERNET_TRACE(DEBUG, self->trace_sink_)
            << "SendChunk::format: ofs=" << chunk.offset
            << " len=" << chunk.slice.length()
            << " desired_prefix=" << args.desired_prefix
            << " max_length=" << args.max_length
            << " message_id_length=" << (int)self->message_id_length_;
        assert(args.max_length >
               self->message_id_length_ + varint::WireSizeFor(chunk.offset));
        uint64_t take_len = varint::MaximumLengthWithPrefix(
            args.max_length - self->message_id_length_);
        OVERNET_TRACE(DEBUG, self->trace_sink_) << "TAKE " << take_len;
        if (take_len < chunk.slice.length()) {
          Chunk first = chunk.TakeUntilSliceOffset(take_len);
          self->SendChunk(std::move(chunk));
          chunk = std::move(first);
        }
        return MessageFragment(self->message_id_, std::move(chunk))
            .Write(args.desired_prefix);
      },
      std::move(on_ack));
}

PacketProtocol::SendCallback DatagramStream::SendOp::MakeAckCallback(
    const Chunk& chunk) {
  switch (stream_->reliability_and_ordering_) {
    case ReliabilityAndOrdering::ReliableOrdered:
    case ReliabilityAndOrdering::ReliableUnordered:
      return [chunk, self = OutstandingOp(this)](const Status& status) mutable {
        self->CompleteReliable(status, std::move(chunk));
      };
    case ReliabilityAndOrdering::UnreliableOrdered:
    case ReliabilityAndOrdering::UnreliableUnordered:
      return [self = OutstandingOp(this)](const Status& status) {
        self->CompleteUnreliable(status);
      };
    case ReliabilityAndOrdering::TailReliable:
      return [chunk, self = OutstandingOp(this)](const Status& status) mutable {
        if (self->message_id_ + 1 == self->stream_->next_message_id_) {
          self->CompleteReliable(status, std::move(chunk));
        } else {
          self->CompleteUnreliable(status);
        }
      };
  }
}

void DatagramStream::SendOp::CompleteReliable(const Status& status,
                                              Chunk chunk) {
  if (state_ == State::CLOSED_WITH_ERROR) {
    return;
  }
  if (status.code() == StatusCode::UNAVAILABLE) {
    // Send failed, still open, and retryable: retry.
    SendChunk(std::move(chunk));
  }
}

void DatagramStream::SendOp::CompleteUnreliable(const Status& status) {
  if (status.is_error()) {
    SetClosed(status);
  }
}

///////////////////////////////////////////////////////////////////////////////
// ReceiveOp

DatagramStream::ReceiveOp::ReceiveOp(DatagramStream* stream)
    : trace_sink_(stream->trace_sink_.Decorate([this](const std::string& msg) {
        std::ostringstream out;
        out << "ReceiveOp[" << this << "] " << msg;
        return out.str();
      })) {
  stream->unclaimed_receives_.PushBack(this);
  stream->MaybeContinueReceive();
}

void DatagramStream::ReceiveOp::Pull(StatusOrCallback<Optional<Slice>> ready) {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "Pull incoming_message=" << incoming_message_
      << " pending_close_reason=" << pending_close_reason_;
  if (incoming_message_ == nullptr) {
    if (pending_close_reason_) {
      ready(*pending_close_reason_);
    } else {
      assert(pending_pull_all_.empty());
      pending_pull_ = std::move(ready);
    }
  } else {
    incoming_message_->Pull(std::move(ready));
  }
}

void DatagramStream::ReceiveOp::PullAll(
    StatusOrCallback<std::vector<Slice>> ready) {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "PullAll incoming_message=" << incoming_message_
      << " pending_close_reason=" << pending_close_reason_;
  if (incoming_message_ == nullptr) {
    if (pending_close_reason_) {
      ready(*pending_close_reason_);
    } else {
      assert(pending_pull_.empty());
      pending_pull_all_ = std::move(ready);
    }
  } else {
    incoming_message_->PullAll(std::move(ready));
  }
}

void DatagramStream::ReceiveOp::Close(const Status& status) {
  OVERNET_TRACE(DEBUG, trace_sink_)
      << "Close incoming_message=" << incoming_message_
      << " pending_close_reason=" << pending_close_reason_
      << " status=" << status;
  if (incoming_message_ == nullptr) {
    pending_close_reason_ = status;
    if (!pending_pull_.empty()) {
      if (status.is_error()) {
        pending_pull_(status);
      } else {
        pending_pull_(Nothing);
      }
    }
    if (!pending_pull_all_.empty()) {
      if (status.is_error()) {
        pending_pull_all_(status);
      } else {
        pending_pull_all_(std::vector<Slice>{});
      }
    }
  } else {
    incoming_message_->Close(status);
  }
}

}  // namespace overnet
