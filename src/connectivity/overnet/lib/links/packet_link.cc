// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/packet_link.h"

#include <iostream>
#include <sstream>

namespace overnet {

PacketLink::PacketLink(Router* router, NodeId peer, uint32_t mss,
                       uint64_t label)
    : router_(router),
      timer_(router->timer()),
      peer_(peer),
      label_(label),
      protocol_{router_->timer(), [router] { return (*router->rng())(); }, this,
                PacketProtocol::NullCodec(), mss} {}

void PacketLink::Close(Callback<void> quiesced) {
  ScopedModule<PacketLink> scoped_module(this);
  stashed_.Reset();
  while (!outgoing_.empty()) {
    outgoing_.pop();
  }
  protocol_.Close(std::move(quiesced));
}

void PacketLink::Forward(Message message) {
  // TODO(ctiller): do some real thinking about what this value should be
  constexpr size_t kMaxBufferedMessages = 32;
  ScopedModule<PacketLink> scoped_module(this);
  if (outgoing_.size() >= kMaxBufferedMessages) {
    auto drop = std::move(outgoing_.front());
    outgoing_.pop();
  }
  bool send_immediately = !sending_ && outgoing_.empty();
  OVERNET_TRACE(DEBUG) << "Forward sending=" << sending_
                       << " outgoing=" << outgoing_.size()
                       << " imm=" << send_immediately;
  outgoing_.emplace(std::move(message));
  if (send_immediately) {
    SchedulePacket();
  }
}

void PacketLink::Tombstone() {
  metrics_version_ = fuchsia::overnet::protocol::METRIC_VERSION_TOMBSTONE;
}

fuchsia::overnet::protocol::LinkStatus PacketLink::GetLinkStatus() {
  ScopedModule<PacketLink> scoped_module(this);

  if (metrics_version_ ==
      fuchsia::overnet::protocol::METRIC_VERSION_TOMBSTONE) {
    return fuchsia::overnet::protocol::LinkStatus{router_->node_id().as_fidl(),
                                                  peer_.as_fidl(), label_,
                                                  metrics_version_};
  }

  // Advertise MSS as smaller than it is to account for some bugs that exist
  // right now.
  // TODO(ctiller): eliminate this - we should be precise.
  constexpr uint32_t kUnderadvertiseMaximumSendSize = 32;
  fuchsia::overnet::protocol::LinkStatus m{router_->node_id().as_fidl(),
                                           peer_.as_fidl(), label_,
                                           metrics_version_++};
  m.metrics.set_bw_link(protocol_.bottleneck_bandwidth().bits_per_second());
  m.metrics.set_rtt(protocol_.round_trip_time().as_us());
  m.metrics.set_mss(
      std::max(kUnderadvertiseMaximumSendSize, protocol_.maximum_send_size()) -
      kUnderadvertiseMaximumSendSize);
  return m;
}

void PacketLink::SchedulePacket() {
  assert(!sending_);
  assert(!outgoing_.empty() || stashed_.has_value());
  auto r = new LinkSendRequest(this);
  OVERNET_TRACE(DEBUG) << "Schedule " << r;
  protocol_.Send(PacketProtocol::SendRequestHdl(r));
}

PacketLink::LinkSendRequest::LinkSendRequest(PacketLink* link) : link_(link) {
  assert(!link->sending_);
  link->sending_ = true;
}

PacketLink::LinkSendRequest::~LinkSendRequest() { assert(!blocking_sends_); }

Slice PacketLink::LinkSendRequest::GenerateBytes(LazySliceArgs args) {
  auto link = link_;
  ScopedModule<PacketLink> scoped_module(link_);
  ScopedOp scoped_op(op_);
  OVERNET_TRACE(DEBUG) << "LinkSendRequest[" << this << "]: GenerateBytes";
  assert(blocking_sends_);
  assert(link->sending_);
  blocking_sends_ = false;
  auto pkt = link->BuildPacket(args);
  link->sending_ = false;
  OVERNET_TRACE(DEBUG) << "LinkSendRequest[" << this << "]: Generated " << pkt;
  if (link->stashed_.has_value() || !link->outgoing_.empty()) {
    link->SchedulePacket();
  }
  return pkt;
}

void PacketLink::LinkSendRequest::Ack(const Status& status) {
  ScopedModule<PacketLink> scoped_module(link_);
  ScopedOp scoped_op(op_);
  OVERNET_TRACE(DEBUG) << "LinkSendRequest[" << this
                       << "]: Ack status=" << status
                       << " blocking_sends=" << blocking_sends_;
  if (blocking_sends_) {
    assert(status.is_error());
    assert(link_->sending_);
    link_->sending_ = false;
    blocking_sends_ = false;
  }
  delete this;
}

Slice PacketLink::BuildPacket(LazySliceArgs args) {
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
    auto serialized = wire.Write(router_->node_id(), peer_, std::move(payload));
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
        outgoing_.front().header.MaxPayloadLength(router_->node_id(), peer_,
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

void PacketLink::SendPacket(SeqNum seq, LazySlice data) {
  if (send_packet_queue_ != nullptr) {
    send_packet_queue_->emplace(std::move(data));
    return;
  }

  PacketProtocol::ProtocolRef protocol_ref(&protocol_);
  std::queue<LazySlice> send_packet_queue;
  send_packet_queue_ = &send_packet_queue;

  for (;;) {
    const auto prefix_length = 1 + seq.wire_length();
    auto data_slice = data(
        LazySliceArgs{Border::Prefix(prefix_length),
                      protocol_.maximum_send_size() - prefix_length, false});
    auto send_slice = data_slice.WithPrefix(prefix_length, [seq](uint8_t* p) {
      *p++ = 0;
      seq.Write(p);
    });
    OVERNET_TRACE(DEBUG) << "Emit " << send_slice;
    stats_.outgoing_packet_count++;
    Emit(std::move(send_slice));

    if (send_packet_queue.empty()) {
      break;
    }

    data = std::move(send_packet_queue.front());
    send_packet_queue.pop();
  }

  send_packet_queue_ = nullptr;
}

void PacketLink::Process(TimeStamp received, Slice packet) {
  stats_.incoming_packet_count++;
  ScopedModule<PacketLink> scoped_module(this);
  const uint8_t* const begin = packet.begin();
  const uint8_t* p = begin;
  const uint8_t* const end = packet.end();

  if (p == end) {
    OVERNET_TRACE(WARNING) << "Empty packet";
    return;
  }
  if (*p != 0) {
    OVERNET_TRACE(WARNING) << "Non-zero op-code received in PacketLink";
    return;
  }
  ++p;

  // Packets without sequence numbers are used to end the three way handshake.
  if (p == end)
    return;

  auto seq_status = SeqNum::Parse(&p, end);
  if (seq_status.is_error()) {
    OVERNET_TRACE(WARNING) << "Packet seqnum parse failure: "
                           << seq_status.AsStatus();
    return;
  }
  packet.TrimBegin(p - begin);
  // begin, p, end are no longer valid.
  protocol_.Process(
      received, *seq_status.get(), std::move(packet),
      [this, received](auto packet_status) {
        if (packet_status.is_error()) {
          if (packet_status.code() != StatusCode::CANCELLED) {
            OVERNET_TRACE(WARNING)
                << "Packet header parse failure: " << packet_status.AsStatus();
          }
          return;
        }
        if (auto* msg = *packet_status) {
          auto body_status = ProcessBody(received, std::move(msg->payload));
          if (body_status.is_error()) {
            OVERNET_TRACE(WARNING)
                << "Packet body parse failure: " << body_status;
            return;
          }
        }
      });
}

Status PacketLink::ProcessBody(TimeStamp received, Slice packet) {
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
        packet.TakeUntilOffset(serialized_length), router_->node_id(), peer_);
    if (msg_status.is_error()) {
      return msg_status.AsStatus();
    }
    router_->Forward(Message::SimpleForwarder(std::move(msg_status->message),
                                              std::move(msg_status->payload),
                                              received));
  }
  return Status::Ok();
}

}  // namespace overnet
