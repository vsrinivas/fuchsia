// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/embedded/zx_channel.h"
#include "src/connectivity/overnet/lib/protocol/coding.h"

namespace overnet {

ZxChannel::ZxChannel() {
  state_tag_ = StateTag::kUnbound;
  new (&state_.unbound) Unbound;
}

ZxChannel::ZxChannel(RouterEndpoint::NewStream new_stream) {
  state_tag_ = StateTag::kBound;
  new (&state_.bound) Bound(std::move(new_stream), this);
}

ZxChannel::~ZxChannel() {
  ZX_ASSERT(closed_);
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

void ZxChannel::Close(Callback<void> quiesced) {
  ZX_ASSERT(!closed_);
  quiesced_ = std::move(quiesced);
  closed_ = true;
  if (refs_ == 0) {
    quiesced_();
  }
}

std::pair<ClosedPtr<ZxChannel>, ClosedPtr<ZxChannel>> ZxChannel::MakePair() {
  auto p = std::pair<ClosedPtr<ZxChannel>, ClosedPtr<ZxChannel>>(
      ClosedPtr<ZxChannel>(new ZxChannel()),
      ClosedPtr<ZxChannel>(new ZxChannel()));
  p.first->state_.unbound.peer = p.second.get();
  p.second->state_.unbound.peer = p.first.get();
  return p;
}

void ZxChannel::Message(
    fuchsia::overnet::protocol::ZirconChannelMessage message) {
  switch (state_tag_) {
    case StateTag::kDisconnected:
      break;
    case StateTag::kUnbound:
      ZX_ASSERT(state_.unbound.peer->state_tag_ == StateTag::kUnbound);
      ZX_ASSERT(state_.unbound.queued.empty());
      state_.unbound.peer->state_.unbound.queued.emplace_back(
          std::move(message));
      break;
    case StateTag::kBound:
      state_.bound.proxy.Message(std::move(message));
      break;
  }
}

void ZxChannel::SetReader(fuchsia::overnet::protocol::ZirconChannel* reader) {
  if (reader == nullptr) {
    ZX_ASSERT(reader_ != nullptr);
    reader_ = nullptr;
    return;
  }

  ZX_ASSERT(reader_ == nullptr);
  reader_ = reader;
  if (state_tag_ == StateTag::kBound) {
    state_.bound.stub.Start();
  }
}

void ZxChannel::Bind(RouterEndpoint::NewStream stream) {
  ZX_ASSERT(state_tag_ == StateTag::kUnbound);
  auto* peer = state_.unbound.peer;
  ZX_ASSERT(peer->state_tag_ == StateTag::kUnbound);
  ZX_ASSERT(peer->state_.unbound.queued.empty());

  peer->state_.unbound.~Unbound();
  peer->state_tag_ = StateTag::kBound;
  new (&peer->state_.bound) Bound(std::move(stream), peer);
  if (peer->reader_ != nullptr) {
    peer->state_.bound.stub.Start();
  }

  for (auto& msg : state_.unbound.queued) {
    peer->Message(std::move(msg));
  }

  state_.unbound.~Unbound();
  state_tag_ = StateTag::kDisconnected;
}

void ZxChannel::Encode(internal::Encoder* encoder, size_t offset) {
  Bind(encoder->AppendHandle(
      offset,
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      [](fuchsia::overnet::protocol::ZirconHandle* zh, StreamId stream_id) {
        zh->set_channel(
            fuchsia::overnet::protocol::ChannelHandle{stream_id.as_fidl()});
      }));
}

ClosedPtr<ZxChannel> ZxChannel::Decode(internal::Decoder* decoder,
                                       size_t offset) {
  auto stream = decoder->ClaimHandle(
      offset,
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableOrdered,
      [](fuchsia::overnet::protocol::ZirconHandle zh)
          -> Optional<fuchsia::overnet::protocol::StreamId> {
        if (zh.Which() !=
            fuchsia::overnet::protocol::ZirconHandle::Tag::kChannel) {
          return Nothing;
        }
        return zh.channel().stream_id;
      });
  if (!stream.has_value()) {
    return nullptr;
  }
  return ClosedPtr<ZxChannel>(new ZxChannel(std::move(*stream)));
}

void ZxChannel::Proxy::Send_(fidl::Message message) {
  assert(message.handles().size() == 0);
  auto send_slice = *overnet::Encode(Slice::FromContainer(message.bytes()));
  RouterEndpoint::Stream::SendOp(stream_, send_slice.length())
      .Push(std::move(send_slice), [] {});
}

void ZxChannel::Stub::Send_(fidl::Message message) {
  // ZxChannel protocol is unidirection => there is no sending from stub.
  abort();
}

void ZxChannel::Stub::Message(
    fuchsia::overnet::protocol::ZirconChannelMessage message) {
  auto channel = channel_;
  auto reader = channel->reader_;
  reader->Message(std::move(message));
  // channel_->reader_->Message(std::move(message));
}

void ZxChannel::Stub::Start() {
  recv_op_.Reset(stream_);
  channel_->Ref();
  recv_op_->PullAll(
      [this](overnet::StatusOr<overnet::Optional<std::vector<overnet::Slice>>>
                 status) {
        OVERNET_TRACE(DEBUG) << "Stub read got " << status;
        if (status.is_error() || !status->has_value()) {
          // If a read failed, finish up.
          return;
        }
        auto decode_status = overnet::Decode(
            Slice::AlignedJoin((*status)->begin(), (*status)->end()));
        if (decode_status.is_error()) {
          // Failed to decode: close stream
          stream_->Close(decode_status.AsStatus(), Callback<void>::Ignored());
          return;
        }
        auto packet = Slice::Aligned(std::move(*decode_status));
        if (auto process_status = Process_(fidl::Message(
                fidl::BytePart(const_cast<uint8_t*>(packet.begin()),
                               packet.length(), packet.length()),
                fidl::HandlePart()));
            process_status != ZX_OK) {
          stream_->Close(Status::FromZx(process_status)
                             .WithContext("Processing ZxChannel stub message"),
                         Callback<void>::Ignored());
          return;
        }
        if (!channel_->closed_) {
          Start();
        }
        channel_->Unref();
      });
}

RouterEndpoint::Stream* ZxChannel::overnet_stream() {
  ZX_ASSERT(state_tag_ == StateTag::kBound);
  return state_.bound.stream.get();
}

}  // namespace overnet
