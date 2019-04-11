// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/zx_socket.h"

namespace overnet {

ZxSocket::ZxSocket(uint32_t options) : options_(options) {
  state_tag_ = StateTag::kUnbound;
  new (&state_.unbound) Unbound;
}

ZxSocket::ZxSocket(uint32_t options, RouterEndpoint::NewStream new_stream)
    : options_(options) {
  state_tag_ = StateTag::kBound;
  new (&state_.bound) Bound(std::move(new_stream));
}

ZxSocket::~ZxSocket() {
  switch (state_tag_) {
    case StateTag::kDisconnected:
      break;
    case StateTag::kUnbound:
      ZX_ASSERT(state_.unbound.peer->state_tag_ == StateTag::kUnbound);
      state_.unbound.peer->state_.unbound.~Unbound();
      state_.unbound.peer->state_tag_ = StateTag::kDisconnected;
      state_.unbound.~Unbound();
      break;
    case StateTag::kBound:
      state_.bound.~Bound();
      break;
  }
}

std::pair<ClosedPtr<ZxSocket>, ClosedPtr<ZxSocket>> ZxSocket::MakePair(
    uint32_t options) {
  auto p = std::pair<ClosedPtr<ZxSocket>, ClosedPtr<ZxSocket>>(
      ClosedPtr<ZxSocket>(new ZxSocket(options)),
      ClosedPtr<ZxSocket>(new ZxSocket(options)));
  p.first->state_.unbound.peer = p.second.get();
  p.second->state_.unbound.peer = p.first.get();
  return p;
}

void ZxSocket::Message(std::vector<uint8_t> bytes) {
  switch (state_tag_) {
    case StateTag::kDisconnected:
      break;
    case StateTag::kUnbound:
      ZX_ASSERT(state_.unbound.peer->state_tag_ == StateTag::kUnbound);
      ZX_ASSERT(state_.unbound.queued.empty());
      state_.unbound.peer->state_.unbound.queued.emplace_back(
          QueueSlot{QueueItem::kMessage, std::move(bytes), {}});
      break;
    case StateTag::kBound:
      state_.bound.proxy.Message(std::move(bytes));
      break;
  }
}

void ZxSocket::Control(std::vector<uint8_t> bytes) {
  switch (state_tag_) {
    case StateTag::kDisconnected:
      break;
    case StateTag::kUnbound:
      ZX_ASSERT(state_.unbound.peer->state_tag_ == StateTag::kUnbound);
      ZX_ASSERT(state_.unbound.queued.empty());
      state_.unbound.peer->state_.unbound.queued.emplace_back(
          QueueSlot{QueueItem::kControl, std::move(bytes), {}});
      break;
    case StateTag::kBound:
      state_.bound.proxy.Control(std::move(bytes));
      break;
  }
}

void ZxSocket::Share(fuchsia::overnet::protocol::SocketHandle handle) {
  switch (state_tag_) {
    case StateTag::kDisconnected:
      break;
    case StateTag::kUnbound:
      ZX_ASSERT(state_.unbound.peer->state_tag_ == StateTag::kUnbound);
      ZX_ASSERT(state_.unbound.queued.empty());
      state_.unbound.peer->state_.unbound.queued.emplace_back(
          QueueSlot{QueueItem::kControl, {}, std::move(handle)});
      break;
    case StateTag::kBound:
      state_.bound.proxy.Share(std::move(handle));
      break;
  }
}

void ZxSocket::Encode(internal::Encoder* encoder, size_t offset) {
  ZX_ASSERT(state_tag_ == StateTag::kUnbound);
  auto* peer = state_.unbound.peer;
  ZX_ASSERT(peer->state_tag_ == StateTag::kUnbound);
  ZX_ASSERT(peer->state_.unbound.queued.empty());
  auto stream = encoder->AppendHandle(
      offset,
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      [options = options_](fuchsia::overnet::protocol::ZirconHandle* zh,
                           StreamId stream_id) {
        zh->set_socket(fuchsia::overnet::protocol::SocketHandle{
            stream_id.as_fidl(), options});
      });
  peer->state_.unbound.~Unbound();
  peer->state_tag_ = StateTag::kBound;
  new (&peer->state_.bound) Bound(std::move(stream));

  for (auto& msg : state_.unbound.queued) {
    switch (msg.item) {
      case QueueItem::kMessage:
        peer->Message(std::move(msg.bytes));
        break;
      case QueueItem::kControl:
        peer->Control(std::move(msg.bytes));
        break;
      case QueueItem::kShare:
        peer->Share(std::move(msg.handle));
        break;
    }
  }

  state_.unbound.~Unbound();
  state_tag_ = StateTag::kDisconnected;
}

ClosedPtr<ZxSocket> ZxSocket::Decode(internal::Decoder* decoder,
                                     size_t offset) {
  uint32_t options;
  auto stream = decoder->ClaimHandle(
      offset,
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      [&options](fuchsia::overnet::protocol::ZirconHandle zh)
          -> Optional<fuchsia::overnet::protocol::StreamId> {
        if (zh.Which() !=
            fuchsia::overnet::protocol::ZirconHandle::Tag::kSocket) {
          return Nothing;
        }
        options = zh.socket().options;
        return zh.socket().stream_id;
      });
  if (!stream.has_value()) {
    return nullptr;
  }
  return ClosedPtr<ZxSocket>(new ZxSocket(options, std::move(*stream)));
}

void ZxSocket::Proxy::Send_(fidl::Message message) {
  assert(message.handles().size() == 0);
  auto send_slice = overnet::Slice::FromContainer(message.bytes());
  overnet::RouterEndpoint::Stream::SendOp(stream_, send_slice.length())
      .Push(std::move(send_slice), [] {});
}

RouterEndpoint::Stream* ZxSocket::overnet_stream() {
  ZX_ASSERT(state_tag_ == StateTag::kBound);
  return state_.bound.stream.get();
}

}  // namespace overnet
