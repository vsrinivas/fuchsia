// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/datagram_stream/datagram_stream.h"
#include <sstream>

namespace overnet {

////////////////////////////////////////////////////////////////////////////////
// MessageFragment

Slice MessageFragment::Write(Border desired_border) const {
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
    case Type::MessageCancel:
    case Type::StreamEnd: {
      const Status& status = this->status();
      const auto& reason = status.reason();
      const auto reason_length_length = varint::WireSizeFor(reason.length());
      const auto frame_length =
          1 + message_length + 1 + reason_length_length + reason.length();
      return Slice::WithInitializerAndBorders(
          frame_length, desired_border, [&](uint8_t* bytes) {
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
    case Type::MessageCancel:
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

DatagramStream::DatagramStream(
    Router* router, NodeId peer,
    fuchsia::overnet::protocol::ReliabilityAndOrdering reliability_and_ordering,
    StreamId stream_id)
    : timer_(router->timer()),
      router_(router),
      peer_(peer),
      stream_id_(stream_id),
      reliability_and_ordering_(reliability_and_ordering),
      receive_mode_(reliability_and_ordering),
      // TODO(ctiller): What should mss be? Hardcoding to 2048 for now.
      packet_protocol_(
          timer_, [router] { return (*router->rng())(); }, this,
          PacketProtocol::NullCodec(), 2048) {}

void DatagramStream::Register() {
  ScopedModule<DatagramStream> scoped_module(this);
  if (router_->RegisterStream(peer_, stream_id_, this).is_error()) {
    abort();
  }
}

DatagramStream::~DatagramStream() {
  ScopedModule<DatagramStream> scoped_module(this);
  assert(close_state_ == CloseState::QUIESCED);
  assert(stream_refs_ == 0);
}

void DatagramStream::Close(const Status& status, Callback<void> quiesced) {
  ScopedModule<DatagramStream> scoped_module(this);
  OVERNET_TRACE(DEBUG) << "Close: state=" << close_state_
                       << " status=" << status << " peer=" << peer_
                       << " stream_id=" << stream_id_;

  switch (close_state_) {
    case CloseState::QUIESCED:
      return;
    case CloseState::CLOSED:
      on_quiesced_.push_back(std::move(quiesced));
      return;
    case CloseState::LOCAL_CLOSE_REQUESTED_OK:
      if (status.is_error()) {
        close_state_ = CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR;
        local_close_status_ = status;
        CancelReceives();
      }
      [[fallthrough]];
    case CloseState::CLOSING_PROTOCOL:
    case CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR:
      on_quiesced_.emplace_back(std::move(quiesced));
      return;
    case CloseState::DRAINING_LOCAL_CLOSED_OK:
      on_quiesced_.emplace_back(std::move(quiesced));
      if (status.is_error()) {
        close_state_ = CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR;
        FinishClosing();
      }
      break;
    case CloseState::REMOTE_CLOSED:
      on_quiesced_.emplace_back(std::move(quiesced));
      FinishClosing();
      return;
    case CloseState::OPEN:
      close_state_ = status.is_error()
                         ? CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR
                         : CloseState::LOCAL_CLOSE_REQUESTED_OK;
      if (status.is_error()) {
        CancelReceives();
      }
      local_close_status_ = status;
      on_quiesced_.emplace_back(std::move(quiesced));
      receive_mode_.Close(status);
      SendCloseAndFlushQuiesced(0);
      return;
  }
}

void DatagramStream::SendCloseAndFlushQuiesced(int retry_number) {
  constexpr int kMaxCloseRetries = 3;
  assert(close_state_ == CloseState::LOCAL_CLOSE_REQUESTED_OK ||
         close_state_ == CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR);
  OVERNET_TRACE(DEBUG) << "SendClose: status=" << *local_close_status_
                       << " retry=" << retry_number
                       << " state=" << close_state_;
  packet_protocol_.Send(
      [this](auto args) {
        ScopedModule<DatagramStream> scoped_module(this);
        OVERNET_TRACE(DEBUG)
            << "SendClose WRITE next_message_id=" << next_message_id_
            << " local_close_status=" << local_close_status_;
        return MessageFragment::EndOfStream(next_message_id_,
                                            *local_close_status_)
            .Write(args.desired_border);
      },
      [this, retry_number](const Status& send_status) mutable {
        ScopedModule<DatagramStream> scoped_module(this);
        OVERNET_TRACE(DEBUG)
            << "SendClose ACK status=" << send_status
            << " retry=" << retry_number << " close_state=" << close_state_;
        switch (close_state_) {
          case CloseState::OPEN:
          case CloseState::REMOTE_CLOSED:
          case CloseState::DRAINING_LOCAL_CLOSED_OK:
            assert(false);
            break;
          case CloseState::QUIESCED:
          case CloseState::CLOSED:
          case CloseState::CLOSING_PROTOCOL:
            break;
          case CloseState::LOCAL_CLOSE_REQUESTED_OK:
          case CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR:
            if (send_status.code() == StatusCode::UNAVAILABLE &&
                retry_number != kMaxCloseRetries) {
              SendCloseAndFlushQuiesced(retry_number + 1);
            } else {
              if (send_status.is_error()) {
                close_state_ = CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR;
              } else {
                close_state_ = CloseState::DRAINING_LOCAL_CLOSED_OK;
              }
              MaybeFinishClosing();
            }
            break;
        }
      });
}

void DatagramStream::MaybeFinishClosing() {
  OVERNET_TRACE(DEBUG) << "MaybeFinishClosing: state=" << close_state_
                       << " message_state.size()==" << message_state_.size();
  switch (close_state_) {
    case CloseState::OPEN:
    case CloseState::REMOTE_CLOSED:
    case CloseState::CLOSED:
    case CloseState::QUIESCED:
    case CloseState::CLOSING_PROTOCOL:
    case CloseState::LOCAL_CLOSE_REQUESTED_OK:
      return;
    case CloseState::DRAINING_LOCAL_CLOSED_OK:
      if (message_state_.empty()) {
        FinishClosing();
      }
      break;
    case CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR:
      FinishClosing();
      break;
  }
}

void DatagramStream::CancelReceives() {
  while (!unclaimed_receives_.Empty()) {
    unclaimed_receives_.begin()->Close(Status::Cancelled());
  }
}

void DatagramStream::FinishClosing() {
  OVERNET_TRACE(DEBUG) << "FinishClosing: state=" << close_state_;
  assert(close_state_ == CloseState::DRAINING_LOCAL_CLOSED_OK ||
         close_state_ == CloseState::LOCAL_CLOSE_REQUESTED_OK ||
         close_state_ == CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR ||
         close_state_ == CloseState::REMOTE_CLOSED);
  close_state_ = CloseState::CLOSING_PROTOCOL;
  CancelReceives();
  packet_protocol_.Close([this]() {
    OVERNET_TRACE(DEBUG) << "FinishClosing/ProtocolClosed: state="
                         << close_state_;
    close_state_ = CloseState::CLOSED;

    auto unregister_status = router_->UnregisterStream(peer_, stream_id_, this);
    assert(unregister_status.is_ok());

    std::vector<PendingSend> pending_send;
    pending_send.swap(pending_send_);
    pending_send.clear();

    assert(message_state_.empty());

    close_ref_.Abandon();
  });
}

void DatagramStream::Quiesce() {
  assert(close_state_ == CloseState::CLOSED);
  close_state_ = CloseState::QUIESCED;
  std::vector<Callback<void>> on_quiesced;
  on_quiesced.swap(on_quiesced_);
  on_quiesced.clear();
}

template <typename F>
struct NoDiscard {
  F f;
  NoDiscard(F const& f) : f(f) {}
  template <typename... T>
  [[nodiscard]] constexpr auto operator()(T&&... t) noexcept(
      noexcept(f(std::forward<T>(t)...))) {
    return f(std::forward<T>(t)...);
  }
};

void DatagramStream::HandleMessage(SeqNum seq, TimeStamp received, Slice data) {
  ScopedModule<DatagramStream> scoped_module(this);

  OVERNET_TRACE(DEBUG) << "DatagramStream.HandleMessage: data=" << data
                       << " close_state=" << close_state_;

  packet_protocol_.Process(
      received, seq, std::move(data), [this](auto status_or_message) {
        switch (close_state_) {
          // In these states we process messages:
          case CloseState::OPEN:
          case CloseState::LOCAL_CLOSE_REQUESTED_OK:
          case CloseState::REMOTE_CLOSED:
          case CloseState::DRAINING_LOCAL_CLOSED_OK:
            break;
          // In these states we do not:
          case CloseState::CLOSING_PROTOCOL:
          case CloseState::CLOSED:
          case CloseState::QUIESCED:
          case CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR:
            return;
        }
        if (status_or_message.is_error()) {
          OVERNET_TRACE(WARNING)
              << "Failed to process packet: " << status_or_message.AsStatus();
          return;
        }
        if (!*status_or_message) {
          return;
        }
        auto payload = std::move((*status_or_message)->payload);
        OVERNET_TRACE(DEBUG) << "Process payload " << payload;
        auto msg_status = MessageFragment::Parse(std::move(payload));
        if (msg_status.is_error()) {
          OVERNET_TRACE(WARNING)
              << "Failed to parse message: " << msg_status.AsStatus();
          return;
        }
        auto msg = std::move(*msg_status.get());
        OVERNET_TRACE(DEBUG) << "Payload type=" << static_cast<int>(msg.type())
                             << " msg=" << msg.message();
        switch (msg.type()) {
          case MessageFragment::Type::Chunk: {
            OVERNET_TRACE(DEBUG)
                << "chunk offset=" << msg.chunk().offset
                << " length=" << msg.chunk().slice.length()
                << " end-of-message=" << msg.chunk().end_of_message;
            // Got a chunk of data: add it to the relevant incoming message.
            largest_incoming_message_id_seen_ =
                std::max(largest_incoming_message_id_seen_, msg.message());
            const uint64_t msg_id = msg.message();
            auto it = messages_.find(msg_id);
            if (it == messages_.end()) {
              it = messages_
                       .emplace(std::piecewise_construct,
                                std::forward_as_tuple(msg_id),
                                std::forward_as_tuple(this, msg_id))
                       .first;
              if (!it->second.Push(std::move(*msg.mutable_chunk()))) {
                (*status_or_message)->Nack();
              }
              receive_mode_.Begin(
                  msg_id, [this, msg_id](const Status& status) mutable {
                    if (status.is_error()) {
                      OVERNET_TRACE(WARNING) << "Receive failed: " << status;
                      return;
                    }
                    auto it = messages_.find(msg_id);
                    if (it == messages_.end()) {
                      return;
                    }
                    unclaimed_messages_.PushBack(&it->second);
                    MaybeContinueReceive();
                  });
            } else {
              if (!it->second.Push(std::move(*msg.mutable_chunk()))) {
                (*status_or_message)->Nack();
              }
            }
          } break;
          case MessageFragment::Type::MessageCancel: {
            // Aborting a message: this is like a close to the incoming message.
            largest_incoming_message_id_seen_ =
                std::max(largest_incoming_message_id_seen_, msg.message());
            auto it = messages_.find(msg.message());
            if (it == messages_.end()) {
              it = messages_
                       .emplace(std::piecewise_construct,
                                std::forward_as_tuple(msg.message()),
                                std::forward_as_tuple(this, msg.message()))
                       .first;
            }
            it->second.Close(msg.status()).Ignore();
          } break;
          case MessageFragment::Type::StreamEnd:
            // TODO(ctiller): handle case of ok termination with outstanding
            // messages.
            RequestedClose requested_close{msg.message(), msg.status()};
            OVERNET_TRACE(DEBUG)
                << "peer requests close with status " << msg.status();
            if (requested_close_.has_value()) {
              if (*requested_close_ != requested_close) {
                OVERNET_TRACE(WARNING)
                    << "Failed to parse message: " << msg_status.AsStatus();
                return;
              }
              return;
            }
            requested_close_ = requested_close;
            auto enact_remote_close = [this](const Status& status) {
              switch (close_state_) {
                case CloseState::OPEN:
                  close_state_ = CloseState::REMOTE_CLOSED;
                  receive_mode_.Close(status);
                  break;
                case CloseState::REMOTE_CLOSED:
                  assert(false);
                  break;
                case CloseState::LOCAL_CLOSE_REQUESTED_OK:
                case CloseState::LOCAL_CLOSE_REQUESTED_WITH_ERROR:
                case CloseState::DRAINING_LOCAL_CLOSED_OK:
                  FinishClosing();
                  break;
                case CloseState::CLOSED:
                case CloseState::QUIESCED:
                case CloseState::CLOSING_PROTOCOL:
                  break;
              }
            };
            if (requested_close_->status.is_error()) {
              enact_remote_close(requested_close_->status);
            } else {
              receive_mode_.Begin(msg.message(), std::move(enact_remote_close));
            }
        }
      });
}

void DatagramStream::MaybeContinueReceive() {
  OVERNET_TRACE(DEBUG) << "MaybeContinueReceive: unclaimed_messages="
                       << unclaimed_messages_.Size()
                       << " unclaimed_receives=" << unclaimed_receives_.Size();

  if (unclaimed_messages_.Empty())
    return;
  if (unclaimed_receives_.Empty())
    return;

  auto incoming_message = unclaimed_messages_.PopFront();
  auto receive_op = unclaimed_receives_.PopFront();

  receive_op->incoming_message_ = incoming_message;
  if (!receive_op->pending_pull_.empty()) {
    incoming_message->Pull(std::move(receive_op->pending_pull_));
  } else if (!receive_op->pending_pull_all_.empty()) {
    incoming_message->PullAll(
        [receive_op](StatusOr<Optional<std::vector<Slice>>> status) {
          auto cb = std::move(receive_op->pending_pull_all_);
          receive_op->Close(status.AsStatus());
          cb(std::move(status));
        });
  }
}

void DatagramStream::SendPacket(SeqNum seq, LazySlice data) {
  router_->Forward(
      Message{std::move(RoutableMessage(router_->node_id())
                            .AddDestination(peer_, stream_id_, seq)),
              std::move(data), timer_->Now()});
}

////////////////////////////////////////////////////////////////////////////////
// SendOp

DatagramStream::SendOp::SendOp(DatagramStream* stream, uint64_t payload_length)
    : StateRef(stream,
               stream->message_state_
                   .emplace(std::piecewise_construct,
                            std::forward_as_tuple(stream->next_message_id_++),
                            std::forward_as_tuple())
                   .first),
      payload_length_(payload_length) {
  ScopedModule<DatagramStream> in_dgs(stream);
  ScopedModule<SendOp> in_send_op(this);
  OVERNET_TRACE(DEBUG) << "SendOp created";
}

DatagramStream::SendOp::~SendOp() {
  ScopedModule<DatagramStream> in_dgs(stream());
  ScopedModule<SendOp> in_send_op(this);
  OVERNET_TRACE(DEBUG) << "SendOp destroyed";
}

void DatagramStream::SendError(StateRef state, const overnet::Status& status) {
  ScopedModule<DatagramStream> in_dgs(this);
  OVERNET_TRACE(DEBUG) << "SendError: " << status;
  packet_protocol_.Send(
      [message_id = state.message_id(), status](auto arg) {
        return MessageFragment::Abort(message_id, status)
            .Write(arg.desired_border);
      },
      [state, status](const Status& send_status) {
        if (send_status.code() == StatusCode::UNAVAILABLE &&
            state.stream()->close_state_ == CloseState::OPEN) {
          state.stream()->SendError(state, status);
        }
        OVERNET_TRACE(DEBUG) << "SendError: ACK " << status;
      });
}

void DatagramStream::SendOp::Close(const Status& status) {
  ScopedModule<DatagramStream> in_dgs(stream());
  ScopedModule<SendOp> in_send_op(this);
  if (status.is_ok() && payload_length_ != push_offset_) {
    std::ostringstream out;
    out << "Insufficient bytes for message presented: expected "
        << payload_length_ << " but got " << push_offset_;
    SetClosed(Status(StatusCode::INVALID_ARGUMENT, out.str()));
  } else {
    SetClosed(status);
  }
}

void DatagramStream::StateRef::SetClosed(const Status& status) {
  if (state() != SendState::OPEN) {
    return;
  }
  if (status.is_ok()) {
    set_state(SendState::CLOSED_OK);
  } else {
    set_state(SendState::CLOSED_WITH_ERROR);
    stream()->SendError(*this, status);
  }
}

void DatagramStream::SendOp::Push(Slice item, Callback<void> started) {
  ScopedModule<DatagramStream> in_dgs(stream());
  ScopedModule<SendOp> in_send_op(this);
  assert(state() == SendState::OPEN);
  if (state() != SendState::OPEN || stream()->IsClosedForSending()) {
    OVERNET_TRACE(DEBUG) << "Push: state=" << state()
                         << " close_state=" << stream()->close_state_
                         << " => ignore send: " << item;
    return;
  }
  const auto chunk_start = push_offset_;
  const auto chunk_length = item.length();
  const auto end_byte = chunk_start + chunk_length;
  OVERNET_TRACE(DEBUG) << "Push: chunk_start=" << chunk_start
                       << " chunk_length=" << chunk_length
                       << " end_byte=" << end_byte
                       << " payload_length=" << payload_length_;
  if (end_byte > payload_length_) {
    Close(Status(StatusCode::INVALID_ARGUMENT,
                 "Exceeded message payload length"));
    return;
  }
  push_offset_ += chunk_length;
  Chunk chunk{chunk_start, end_byte == payload_length_, std::move(item)};
  stream()->SendChunk(*this, std::move(chunk), std::move(started));
}

void DatagramStream::SendChunk(StateRef state, Chunk chunk,
                               Callback<void> started) {
  ScopedModule<DatagramStream> in_dgs(this);
  OVERNET_TRACE(DEBUG) << "SchedOutChunk: msg=" << state.message_id()
                       << " ofs=" << chunk.offset
                       << " len=" << chunk.slice.length()
                       << " pending=" << pending_send_.size()
                       << " sending=" << sending_;
  auto it = std::upper_bound(
      pending_send_.begin(), pending_send_.end(),
      std::make_tuple(state.message_id(), chunk.offset),
      [](const std::tuple<uint64_t, uint64_t>& label, const PendingSend& ps) {
        return label < std::make_tuple(ps.what.state.message_id(),
                                       ps.what.chunk.offset);
      });

  if (it != pending_send_.end()) {
    OVERNET_TRACE(DEBUG) << "  prior to msg=" << it->what.state.message_id()
                         << " ofs=" << it->what.chunk.offset
                         << " len=" << it->what.chunk.slice.length();
    if (state.message_id() == it->what.state.message_id()) {
      if (auto joined =
              Chunk::JoinIfSameUnderlyingMemory(chunk, it->what.chunk)) {
        OVERNET_TRACE(DEBUG) << "Merged previously separated chunks";
        it->what.chunk = *joined;
        goto done;
      }
    }
  }
  if (it != pending_send_.begin()) {
    OVERNET_TRACE(DEBUG) << "  after msg=" << (it - 1)->what.state.message_id()
                         << " ofs=" << (it - 1)->what.chunk.offset
                         << " len=" << (it - 1)->what.chunk.slice.length();
    if (state.message_id() == (it - 1)->what.state.message_id()) {
      if (auto joined =
              Chunk::JoinIfSameUnderlyingMemory((it - 1)->what.chunk, chunk)) {
        OVERNET_TRACE(DEBUG) << "Merged previously separated chunks";
        (it - 1)->what.chunk = *joined;
        goto done;
      }
    }
  }
  if (it == pending_send_.begin()) {
    OVERNET_TRACE(DEBUG) << "  at start of queue";
  }
  if (it == pending_send_.end()) {
    OVERNET_TRACE(DEBUG) << "  at end of queue";
  }
  if (chunk.slice.length() == 0 && it == pending_send_.begin()) {
    // Skip adding zero-length chunks at the start of the queue.
    // These are probes anyway that we've reached that point, and so
    // there's no need to do any further work (and this simplifies later logic
    // in the pipeline).
    return;
  }
  pending_send_.emplace(
      it, PendingSend{{std::move(chunk), state}, std::move(started)});

done:
  OVERNET_TRACE(DEBUG) << "Send queue: " << PendingSendString();
  if (!sending_) {
    SendNextChunk();
  }
}

std::string DatagramStream::PendingSendString() {
  std::ostringstream out;
  out << '[';
  bool first = true;
  for (const auto& ps : pending_send_) {
    if (!first) {
      out << ',';
    }
    first = false;
    out << ps.what.state.message_id() << '/' << ps.what.chunk;
  }
  return out.str();
}

void DatagramStream::SendNextChunk() {
  ScopedModule<DatagramStream> in_dgs(this);
  assert(close_state_ == CloseState::OPEN ||
         close_state_ == CloseState::LOCAL_CLOSE_REQUESTED_OK ||
         close_state_ == CloseState::DRAINING_LOCAL_CLOSED_OK);
  assert(!sending_);

  OVERNET_TRACE(DEBUG) << "SendNextChunk: pending=" << pending_send_.size();

  auto first_real_pending = pending_send_.end();
  for (auto it = pending_send_.begin(); it != pending_send_.end(); ++it) {
    if (it->what.chunk.slice.length() > 0) {
      first_real_pending = it;
      break;
    }
    OVERNET_TRACE(DEBUG) << "Skip empty send: " << it->what.chunk;
  }
  pending_send_.erase(pending_send_.begin(), first_real_pending);
  OVERNET_TRACE(DEBUG) << "SendNextChunk': pending=" << pending_send_.size();
  if (pending_send_.empty()) {
    OVERNET_TRACE(DEBUG) << "no need to send";
    return;
  }

  sending_ = true;

  class PullChunk {
   public:
    PullChunk(DatagramStream* stream, const LazySliceArgs* args)
        : send_(Pull(stream, args)), args_(args) {
      assert(this->stream() == stream);
    }
    ~PullChunk() {
      if (!stream()->IsClosedForSending()) {
        stream()->sending_ = false;
        stream()->SendNextChunk();
      }
    }
    Slice Finish() {
      if (send_.chunk.has_value()) {
        return MessageFragment(message_id(), std::move(*send_.chunk))
            .Write(args_->desired_border);
      } else {
        return Slice();
      }
    }

    DatagramStream* stream() const { return send_.state.stream(); }
    uint64_t message_id() const { return send_.state.message_id(); }
    ChunkAndState chunk_and_state() const {
      if (send_.chunk.has_value()) {
        return ChunkAndState{*send_.chunk, send_.state};
      } else {
        return ChunkAndState{Chunk{0, false, Slice()}, send_.state};
      }
    }

   private:
    struct ChunkAndOptState {
      Optional<Chunk> chunk;
      StateRef state;
    };
    ChunkAndOptState send_;
    const LazySliceArgs* const args_;

    static ChunkAndOptState Pull(DatagramStream* stream,
                                 const LazySliceArgs* args) {
      ScopedModule<DatagramStream> in_dgs(stream);
      auto fst = stream->pending_send_.begin();
      auto pending_send = std::move(fst->what);
      auto cb = std::move(fst->started);
      stream->pending_send_.erase(fst);
      // We should remove zero-length chunks before arriving here.
      // Otherwise we cannot ensure that there'll be an actual chunk in the
      // queue.
      assert(pending_send.chunk.slice.length() != 0);
      const auto message_id_length =
          varint::WireSizeFor(pending_send.state.message_id());
      OVERNET_TRACE(DEBUG) << "Format: ofs=" << pending_send.chunk.offset
                           << " len=" << pending_send.chunk.slice.length()
                           << " eom=" << pending_send.chunk.end_of_message
                           << " desired_border=" << args->desired_border
                           << " max_length=" << args->max_length
                           << " message_id_length=" << (int)message_id_length;
      if (args->max_length <=
          message_id_length + varint::WireSizeFor(pending_send.chunk.offset)) {
        stream->SendChunk(pending_send.state, std::move(pending_send.chunk),
                          std::move(cb));
        return ChunkAndOptState{Nothing, pending_send.state};
      }
      assert(args->max_length >
             message_id_length +
                 varint::WireSizeFor(pending_send.chunk.offset));
      uint64_t take_len =
          varint::MaximumLengthWithPrefix(args->max_length - message_id_length);
      OVERNET_TRACE(DEBUG) << "TAKE " << take_len;
      if (take_len < pending_send.chunk.slice.length()) {
        Chunk first = pending_send.chunk.TakeUntilSliceOffset(take_len);
        stream->SendChunk(pending_send.state, std::move(pending_send.chunk),
                          Callback<void>::Ignored());
        pending_send.chunk = std::move(first);
      }
      return ChunkAndOptState{pending_send.chunk, pending_send.state};
    }
  };

  class ReliableChunkSend final : public PacketProtocol::SendRequest {
   public:
    ReliableChunkSend(DatagramStream* stream) : stream_(stream) {}

    Slice GenerateBytes(LazySliceArgs args) override {
      PullChunk pc(stream_, &args);
      sent_ = pc.chunk_and_state();
      return pc.Finish();
    }

    void Ack(const Status& status) override {
      if (sent_) {
        stream_->CompleteReliable(status, std::move(sent_->state),
                                  std::move(sent_->chunk));
      } else if (!stream_->IsClosedForSending()) {
        stream_->sending_ = false;
        stream_->SendNextChunk();
      }
      delete this;
    }

   private:
    DatagramStream* const stream_;
    Optional<ChunkAndState> sent_;
  };

  class UnreliableChunkSend final : public PacketProtocol::SendRequest {
   public:
    UnreliableChunkSend(DatagramStream* stream) : stream_(stream) {}

    Slice GenerateBytes(LazySliceArgs args) override {
      PullChunk pc(stream_, &args);
      sent_ = pc.chunk_and_state().state;
      return pc.Finish();
    }

    void Ack(const Status& status) override {
      if (sent_) {
        stream_->CompleteUnreliable(status, std::move(*sent_));
      } else if (!stream_->IsClosedForSending()) {
        stream_->sending_ = false;
        stream_->SendNextChunk();
      }
      delete this;
    }

   private:
    DatagramStream* const stream_;
    Optional<StateRef> sent_;
  };

  class TailReliableChunkSend final : public PacketProtocol::SendRequest {
   public:
    TailReliableChunkSend(DatagramStream* stream) : stream_(stream) {}

    Slice GenerateBytes(LazySliceArgs args) override {
      PullChunk pc(stream_, &args);
      sent_ = pc.chunk_and_state();
      return pc.Finish();
    }

    void Ack(const Status& status) override {
      if (sent_) {
        if (sent_->state.message_id() + 1 == stream_->next_message_id_) {
          stream_->CompleteReliable(status, std::move(sent_->state),
                                    std::move(sent_->chunk));
        } else {
          stream_->CompleteUnreliable(status, std::move(sent_->state));
        }
      } else if (!stream_->IsClosedForSending()) {
        stream_->sending_ = false;
        stream_->SendNextChunk();
      }
      delete this;
    }

   private:
    DatagramStream* const stream_;
    Optional<ChunkAndState> sent_;
  };

  switch (reliability_and_ordering_) {
    case fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered:
    case fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableUnordered:
      packet_protocol_.Send(
          PacketProtocol::SendRequestHdl(new ReliableChunkSend(this)));
      break;
    case fuchsia::overnet::protocol::ReliabilityAndOrdering::UnreliableOrdered:
    case fuchsia::overnet::protocol::ReliabilityAndOrdering::
        UnreliableUnordered:
      packet_protocol_.Send(
          PacketProtocol::SendRequestHdl(new UnreliableChunkSend(this)));
      break;
    case fuchsia::overnet::protocol::ReliabilityAndOrdering::TailReliable:
      packet_protocol_.Send(
          PacketProtocol::SendRequestHdl(new TailReliableChunkSend(this)));
      break;
  }
}

void DatagramStream::CompleteReliable(const Status& status, StateRef state,
                                      Chunk chunk) {
  ScopedModule<DatagramStream> in_dgs(this);
  OVERNET_TRACE(DEBUG) << "CompleteReliable: status=" << status
                       << " state=" << static_cast<int>(state.state())
                       << " stream_state=" << state.stream()->close_state_;
  if (state.state() == SendState::CLOSED_WITH_ERROR) {
    return;
  }
  if (status.code() == StatusCode::UNAVAILABLE &&
      !state.stream()->IsClosedForSending()) {
    // Send failed, still open, and retryable: retry.
    SendChunk(std::move(state), std::move(chunk), Callback<void>::Ignored());
  }
}

void DatagramStream::CompleteUnreliable(const Status& status, StateRef state) {
  ScopedModule<DatagramStream> in_dgs(this);
  OVERNET_TRACE(DEBUG) << "CompleteUnreliable: status=" << status
                       << " state=" << static_cast<int>(state.state());
  if (status.is_error()) {
    state.SetClosed(status);
  }
}

///////////////////////////////////////////////////////////////////////////////
// ReceiveOp

DatagramStream::ReceiveOp::ReceiveOp(DatagramStream* stream) : stream_(stream) {
  ScopedModule<DatagramStream> in_dgs(stream_.get());
  ScopedModule<ReceiveOp> in_recv_op(this);
  stream->unclaimed_receives_.PushBack(this);
  stream->MaybeContinueReceive();
}

void DatagramStream::ReceiveOp::Pull(StatusOrCallback<Optional<Slice>> ready) {
  ScopedModule<DatagramStream> in_dgs(stream_.get());
  ScopedModule<ReceiveOp> in_recv_op(this);
  OVERNET_TRACE(DEBUG) << "Pull incoming_message=" << incoming_message_;
  if (closed_) {
    ready(Status::Cancelled());
    return;
  } else if (incoming_message_ == nullptr) {
    assert(pending_pull_all_.empty());
    pending_pull_ = std::move(ready);
  } else {
    incoming_message_->Pull(std::move(ready));
  }
}

void DatagramStream::ReceiveOp::PullAll(
    StatusOrCallback<Optional<std::vector<Slice>>> ready) {
  ScopedModule<DatagramStream> in_dgs(stream_.get());
  ScopedModule<ReceiveOp> in_recv_op(this);
  OVERNET_TRACE(DEBUG) << "PullAll incoming_message=" << incoming_message_;
  if (closed_) {
    ready(Status::Cancelled());
  } else if (incoming_message_ == nullptr) {
    assert(pending_pull_.empty());
    pending_pull_all_ = std::move(ready);
  } else {
    pending_pull_all_ = std::move(ready);
    incoming_message_->PullAll(
        [this](StatusOr<Optional<std::vector<Slice>>> status) {
          auto cb = std::move(pending_pull_all_);
          Close(status.AsStatus());
          cb(std::move(status));
        });
  }
}

void DatagramStream::ReceiveOp::Close(const Status& status) {
  if (closed_) {
    return;
  }
  ScopedModule<DatagramStream> in_dgs(stream_.get());
  ScopedModule<ReceiveOp> in_recv_op(this);
  OVERNET_TRACE(DEBUG) << "Close incoming_message=" << incoming_message_
                       << " status=" << status;
  closed_ = true;
  if (incoming_message_ == nullptr) {
    assert(stream_->unclaimed_receives_.Contains(this));
    stream_->unclaimed_receives_.Remove(this);
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
        pending_pull_all_(Nothing);
      }
    }
  } else {
    assert(!stream_->unclaimed_receives_.Contains(this));
    stream_->receive_mode_.Completed(incoming_message_->msg_id(),
                                     incoming_message_->Close(status));
    stream_->messages_.erase(incoming_message_->msg_id());
    incoming_message_ = nullptr;
  }
  stream_.Abandon();
}

}  // namespace overnet
