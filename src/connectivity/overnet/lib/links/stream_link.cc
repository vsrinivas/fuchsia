// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/stream_link.h"

namespace overnet {

StreamLink::StreamLink(Router *router, NodeId peer, uint32_t mss,
                       uint64_t label)
    : mss_(mss), router_(router), peer_(peer), local_id_(label) {}

void StreamLink::Forward(Message message) {
  if (emitting_ || closed_) {
    return;
  }

  // Ensure that we fit into the mss with the routing header
  Optional<size_t> max_payload_length =
      message.header.MaxPayloadLength(router_->node_id(), peer_, mss_);
  if (!max_payload_length.has_value() || *max_payload_length <= 1) {
    // Drop packet (higher layers can resend if needed).
    return;
  }

  auto payload = message.make_payload(LazySliceArgs{
      Border::Prefix(varint::WireSizeFor(mss_)), *max_payload_length, false});

  auto packet =
      message.header.Write(router_->node_id(), peer_, std::move(payload));

  auto packet_length = packet.length();
  auto prefix_length = varint::WireSizeFor(packet_length);

  emitting_ = true;
  Emit(packet.WithPrefix(
           prefix_length,
           [=](uint8_t *p) { varint::Write(packet_length, prefix_length, p); }),
       [this](const Status &status) {
         if (status.is_error()) {
           closed_ = true;
         }
         emitting_ = false;
         MaybeQuiesce();
       });
}

void StreamLink::Process(TimeStamp received, Slice bytes) {
  if (closed_) {
    return;
  }

  buffered_input_.Append(std::move(bytes));

  for (;;) {
    const uint8_t *begin = buffered_input_.begin();
    const uint8_t *p = begin;
    const uint8_t *end = buffered_input_.end();

    uint64_t segment_length;
    if (!varint::Read(&p, end, &segment_length)) {
      if (end - p >= varint::WireSizeFor(mss_)) {
        closed_ = true;
      }
      return;
    }

    if (segment_length > mss_) {
      closed_ = true;
      return;
    }

    if (static_cast<uint64_t>(end - p) < segment_length) {
      return;
    }

    buffered_input_.TrimBegin(p - begin);
    auto message_with_payload =
        RoutableMessage::Parse(buffered_input_.TakeUntilOffset(segment_length),
                               router_->node_id(), peer_);

    if (message_with_payload.is_error()) {
      closed_ = true;
      return;
    }

    router_->Forward(Message::SimpleForwarder(
        std::move(message_with_payload->message),
        std::move(message_with_payload->payload), received));
  }
}

void StreamLink::Close(Callback<void> quiesced) {
  closed_ = true;
  on_quiesced_ = std::move(quiesced);
  MaybeQuiesce();
}

void StreamLink::MaybeQuiesce() {
  if (closed_ && !emitting_ && !on_quiesced_.empty()) {
    auto cb = std::move(on_quiesced_);
    cb();
  }
}

fuchsia::overnet::protocol::LinkStatus StreamLink::GetLinkStatus() {
  return fuchsia::overnet::protocol::LinkStatus{
      router_->node_id().as_fidl(), peer_.as_fidl(), local_id_, 1,
      fuchsia::overnet::protocol::LinkMetrics{}};
}

}  // namespace overnet
