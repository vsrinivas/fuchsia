// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/graph/nodes/input.h"

#include <atomic>

#include "src/media/playback/mediaplayer/graph/nodes/node.h"
#include "src/media/playback/mediaplayer/graph/nodes/output.h"

namespace media_player {
namespace {

// Creates a copy of |original| with |copied_payload_buffer| replacing the
// original's payload buffer. |copied_payload_buffer| may be nullptr if and
// only if |original| has no payload.
PacketPtr CopyPacket(const Packet& original, fbl::RefPtr<PayloadBuffer> copied_payload_buffer) {
  FX_DCHECK(copied_payload_buffer || (original.size() == 0 && !original.payload_buffer()));

  PacketPtr copy =
      Packet::Create(original.pts(), original.pts_rate(), original.keyframe(),
                     original.end_of_stream(), original.size(), std::move(copied_payload_buffer));

  if (original.revised_stream_type()) {
    copy->SetRevisedStreamType(original.revised_stream_type()->Clone());
  }

  return copy;
}

}  // namespace

Input::Input(Node* node, size_t index) : node_(node), index_(index), state_(State::kRefusesPacket) {
  FX_DCHECK(node_);
  RegisterPayloadManagerCallbacks();
}

Input::Input(Input&& input)
    : node_(input.node()), index_(input.index()), state_(input.state_.load()) {
  // We can't move an input that's connected, has a packet or is configured.
  // TODO(dalesat): Make |Input| non-movable.
  FX_DCHECK(input.mate() == nullptr);
  FX_DCHECK(input.packet() == nullptr);
  FX_DCHECK(input.payload_config().mode_ == PayloadMode::kNotConfigured);
  RegisterPayloadManagerCallbacks();
}

Input::~Input() {}

void Input::Connect(Output* output) {
  FX_DCHECK(output);
  FX_DCHECK(!mate_);
  mate_ = output;
}

bool Input::needs_packet() const { return state_.load() == State::kNeedsPacket; }

void Input::PutPacket(PacketPtr packet) {
  FX_DCHECK(packet);
  FX_DCHECK(needs_packet());

  std::atomic_store(&packet_, packet);
  state_.store(State::kHasPacket);
  node_->NeedsUpdate();
}

PacketPtr Input::TakePacket(bool request_another) {
  FX_DCHECK(mate_);

  if (!payload_manager_.ready()) {
    return nullptr;
  }

  PacketPtr no_packet;
  PacketPtr packet = std::atomic_exchange(&packet_, no_packet);

  if (request_another) {
    state_.store(State::kNeedsPacket);
    mate_->node()->NeedsUpdate();
  } else {
    state_.store(State::kRefusesPacket);
  }

  if (!packet) {
    return nullptr;
  }

  size_t size = packet->size();

  fbl::RefPtr<PayloadBuffer> copy_destination_buffer;
  if (!payload_manager_.MaybeAllocatePayloadBufferForCopy(size, &copy_destination_buffer)) {
    // Copying is not required, so we just return the packet.
    return packet;
  }

  if (size == 0) {
    // Copying is required, but there's no payload. Return a new packet with the
    // same attributes as |packet|.
    return CopyPacket(*packet, nullptr);
  }

  if (!copy_destination_buffer) {
    // We just drop the packet, so there will be a glitch.
    // TODO(dalesat): Leave the packet behind so we can try again later.
    // We'll also need a NeedsUpdate when the allocator is no longer empty.
    FX_LOGS(ERROR) << "No buffer for copy, dropping packet.";

    // We needed a packet and couldn't produce one, so we still need one.
    state_.store(State::kNeedsPacket);
    mate_->node()->NeedsUpdate();

    return nullptr;
  }

  // Copy the payload.
  FX_DCHECK(copy_destination_buffer->size() >= size);
  memcpy(copy_destination_buffer->data(), packet->payload(), size);

  // Return a new packet like |packet| but with the new payload buffer.
  return CopyPacket(*packet, std::move(copy_destination_buffer));
}

void Input::RequestPacket() {
  FX_DCHECK(mate_);

  State expected = State::kRefusesPacket;
  if (state_.compare_exchange_strong(expected, State::kNeedsPacket)) {
    mate_->node()->NeedsUpdate();
  }
}

void Input::Flush() { TakePacket(false); }

void Input::RegisterPayloadManagerCallbacks() {
  payload_manager_.RegisterReadyCallbacks(
      [this]() {
        FX_DCHECK(mate_);
        FX_DCHECK(mate_->node());
        mate_->node()->NotifyOutputConnectionReady(mate_->index());
      },
      [this]() {
        FX_DCHECK(node_);
        node_->NotifyInputConnectionReady(index());
      });

  payload_manager_.RegisterNewSysmemTokenCallbacks(
      [this]() {
        FX_DCHECK(mate_);
        FX_DCHECK(mate_->node());
        mate_->node()->NotifyNewOutputSysmemToken(mate_->index());
      },
      [this]() {
        FX_DCHECK(node_);
        node_->NotifyNewInputSysmemToken(index());
      });
}

}  // namespace media_player
