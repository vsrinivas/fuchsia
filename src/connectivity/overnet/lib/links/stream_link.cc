// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/stream_link.h"

namespace overnet {

StreamLink::StreamLink(Router *router, NodeId peer,
                       std::unique_ptr<StreamFramer> framer, uint64_t label)
    : router_(router),
      framer_(std::move(framer)),
      peer_(peer),
      local_id_(label),
      packet_stuffer_(router->node_id(), peer) {}

void StreamLink::Forward(Message message) {
  if (closed_) {
    return;
  }
  if (packet_stuffer_.Forward(std::move(message)) && !emitting_) {
    EmitOne();
  }
}

void StreamLink::SetClosed() {
  closed_ = true;
  packet_stuffer_.DropPendingMessages();
}

void StreamLink::EmitOne() {
  static uint64_t num_forwards = 0;
  auto n = num_forwards++;
  OVERNET_TRACE(TRACE) << "StreamLink::EmitOne[" << n
                       << "]: forward with emitting=" << emitting_
                       << " closed=" << closed_;

  assert(!emitting_);
  assert(!closed_);
  assert(packet_stuffer_.HasPendingMessages());

  auto packet = packet_stuffer_.BuildPacket(
      LazySliceArgs{framer_->desired_border, framer_->maximum_segment_size});

  OVERNET_TRACE(TRACE) << "StreamLink::EmitOne[" << n << "]: emit " << packet;

  emitting_ = true;
  stats_.outgoing_packet_count++;
  Emit(framer_->Frame(std::move(packet)),
       StatusCallback(ALLOCATED_CALLBACK, [this, n](const Status &status) {
         if (status.is_error()) {
           OVERNET_TRACE(ERROR) << "Write failed: " << status;
           SetClosed();
         }
         emitting_ = false;
         OVERNET_TRACE(TRACE) << "StreamLink::EmitOne[" << n << "]: emitted";
         if (closed_) {
           MaybeQuiesce();
         } else if (packet_stuffer_.HasPendingMessages()) {
           EmitOne();
         }
       }));
}

void StreamLink::Process(TimeStamp received, Slice bytes) {
  OVERNET_TRACE(TRACE) << "StreamLink.Read: " << bytes;

  if (closed_) {
    return;
  }

  framer_->Push(std::move(bytes));

  for (;;) {
    auto input = framer_->Pop();
    if (input.is_error()) {
      OVERNET_TRACE(ERROR) << input.AsStatus();
      SetClosed();
      return;
    }
    if (!input->has_value()) {
      return;
    }

    stats_.incoming_packet_count++;

    if (auto status = packet_stuffer_.ParseAndForwardTo(
            received, std::move(**input), router_);
        status.is_error()) {
      OVERNET_TRACE(ERROR) << input.AsStatus();
      SetClosed();
      return;
    }
  }
}

void StreamLink::Close(Callback<void> quiesced) {
  OVERNET_TRACE(DEBUG) << "Stream link closed by router";
  SetClosed();
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
  // Advertise MSS as smaller than it is to account for some bugs that exist
  // right now.
  // TODO(ctiller): eliminate this - we should be precise.
  constexpr size_t kUnderadvertiseMaximumSendSize = 32;
  fuchsia::overnet::protocol::LinkMetrics m;
  m.set_mss(std::max(kUnderadvertiseMaximumSendSize, maximum_segment_size()) -
            kUnderadvertiseMaximumSendSize);
  return fuchsia::overnet::protocol::LinkStatus{router_->node_id().as_fidl(),
                                                peer_.as_fidl(), local_id_, 1,
                                                std::move(m)};
}

}  // namespace overnet
