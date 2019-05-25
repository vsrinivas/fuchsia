// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/packet_stuffer.h"

namespace overnet {

PacketStuffer::PacketStuffer(NodeId my_node_id, NodeId peer_node_id)
    : my_node_id_(my_node_id), peer_node_id_(peer_node_id) {}

bool PacketStuffer::Forward(Message message) {
  // TODO(ctiller): do some real thinking about what this value should be
  constexpr size_t kMaxBufferedMessages = 32;
  if (outgoing_.size() >= kMaxBufferedMessages) {
    auto drop = std::move(outgoing_.front());
    outgoing_.pop();
  }
  const bool was_empty = !HasPendingMessages();
  outgoing_.emplace(std::move(message));
  return was_empty;
}

void PacketStuffer::DropPendingMessages() {
  stashed_.Reset();
  while (!outgoing_.empty()) {
    outgoing_.pop();
  }
}

bool PacketStuffer::HasPendingMessages() const {
  return stashed_.has_value() || !outgoing_.empty();
}

Slice PacketStuffer::BuildPacket(LazySliceArgs args) {
  OVERNET_TRACE(DEBUG)
      << "Build outgoing=" << outgoing_.size() << " stashed="
      << (stashed_ ? [&](){
        std::ostringstream fmt;
        fmt << stashed_->message << "+" << stashed_->payload.length() << "b";
        return fmt.str();
      }() : "nil");
  auto remaining_length = args.max_length;
  auto add_serialized_msg = [&remaining_length, this](
                                const RoutableMessage& wire,
                                Slice payload) -> bool {
    auto serialized =
        wire.Write(my_node_id_, peer_node_id_, std::move(payload));
    const auto serialized_length = serialized.length();
    const auto length_length = varint::WireSizeFor(serialized_length);
    const auto segment_length = length_length + serialized_length;
    OVERNET_TRACE(DEBUG) << "AddMsg segment_length=" << segment_length
                         << " remaining_length=" << remaining_length
                         << (segment_length > remaining_length ? "  => SKIP"
                                                               : "")
                         << "; serialized:" << serialized;
    if (segment_length > remaining_length) {
      return false;
    }
    send_slices_.push_back(serialized.WithPrefix(
        length_length, [length_length, serialized_length](uint8_t* p) {
          varint::Write(serialized_length, length_length, p);
        }));
    remaining_length -= segment_length;
    return true;
  };

  static const uint32_t kMinMSS = 64;
  if (stashed_.has_value()) {
    if (add_serialized_msg(stashed_->message, stashed_->payload)) {
      stashed_.Reset();
    } else {
      if (args.has_other_content) {
        // Skip sending any other messages: we'll retry this message
        // without an ack momentarily.
        remaining_length = 0;
      } else {
        // There's no chance we'll ever send this message: drop it.
        abort();
        stashed_.Reset();
        OVERNET_TRACE(DEBUG) << "drop stashed";
      }
    }
  }
  while (!outgoing_.empty() && remaining_length > kMinMSS) {
    // Ensure there's space with the routing header included.
    Optional<size_t> max_len_before_prefix =
        outgoing_.front().header.MaxPayloadLength(my_node_id_, peer_node_id_,
                                                  remaining_length);
    if (!max_len_before_prefix.has_value() || *max_len_before_prefix <= 1) {
      break;
    }
    // And ensure there's space with the segment length header.
    auto max_len = varint::MaximumLengthWithPrefix(*max_len_before_prefix);
    // Pull out the message.
    Message msg = std::move(outgoing_.front());
    outgoing_.pop();
    // Serialize it.
    auto payload = msg.make_payload(LazySliceArgs{
        Border::None(), std::min(msg.mss, static_cast<uint32_t>(max_len)),
        args.has_other_content || !send_slices_.empty()});
    if (payload.length() == 0) {
      continue;
    }
    // Add the serialized version to the outgoing queue.
    if (!add_serialized_msg(msg.header, payload)) {
      // If it fails, stash it, and retry the next loop around.
      // This may happen if the sender is unable to trim to the maximum length.
      OVERNET_TRACE(DEBUG) << "stash too long";
      stashed_.Reset(std::move(msg.header), std::move(payload));
      break;
    }
  }

  Slice send =
      Slice::Join(send_slices_.begin(), send_slices_.end(),
                  args.desired_border.WithAddedPrefix(SeqNum::kMaxWireLength));
  send_slices_.clear();

  return send;
}

Status PacketStuffer::ParseAndForwardTo(TimeStamp received, Slice packet,
                                        Router* router) const {
  while (packet.length()) {
    const uint8_t* const begin = packet.begin();
    const uint8_t* p = begin;
    const uint8_t* const end = packet.end();

    uint64_t serialized_length;
    if (!varint::Read(&p, end, &serialized_length)) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "Failed to parse segment length");
    }
    assert(end >= p);
    if (static_cast<uint64_t>(end - p) < serialized_length) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "Message body extends past end of packet");
    }
    packet.TrimBegin(p - begin);
    auto msg_status = RoutableMessage::Parse(
        packet.TakeUntilOffset(serialized_length), my_node_id_, peer_node_id_);
    if (msg_status.is_error()) {
      return msg_status.AsStatus();
    }
    router->Forward(Message::SimpleForwarder(std::move(msg_status->message),
                                             std::move(msg_status->payload),
                                             received));
  }
  return Status::Ok();
}

}  // namespace overnet
