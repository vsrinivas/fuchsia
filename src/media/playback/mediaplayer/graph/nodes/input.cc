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
PacketPtr CopyPacket(const Packet& original,
                     fbl::RefPtr<PayloadBuffer> copied_payload_buffer) {
  FXL_DCHECK(copied_payload_buffer ||
             (original.size() == 0 && !original.payload_buffer()));

  PacketPtr copy =
      Packet::Create(original.pts(), original.pts_rate(), original.keyframe(),
                     original.end_of_stream(), original.size(),
                     std::move(copied_payload_buffer));

  if (original.revised_stream_type()) {
    copy->SetRevisedStreamType(original.revised_stream_type()->Clone());
  }

  return copy;
}

}  // namespace

Input::Input(Node* node, size_t index)
    : node_(node), index_(index), state_(State::kRefusesPacket) {
  FXL_DCHECK(node_);
}

Input::Input(Input&& input)
    : node_(input.node()), index_(input.index()), state_(input.state_.load()) {
  // We can't move an input that's connected, has a packet or is configured.
  // TODO(dalesat): Make |Input| non-movable.
  FXL_DCHECK(input.mate() == nullptr);
  FXL_DCHECK(input.packet() == nullptr);
  FXL_DCHECK(input.payload_config().mode_ == PayloadMode::kNotConfigured);
}

Input::~Input() {}

void Input::Connect(Output* output) {
  FXL_DCHECK(output);
  FXL_DCHECK(!mate_);
  mate_ = output;
}

bool Input::needs_packet() const {
  return state_.load() == State::kNeedsPacket;
}

void Input::PutPacket(PacketPtr packet) {
  FXL_DCHECK(packet);
  FXL_DCHECK(needs_packet());

  std::atomic_store(&packet_, packet);
  state_.store(State::kHasPacket);
  node_->NeedsUpdate();
}

PacketPtr Input::TakePacket(bool request_another) {
  FXL_DCHECK(mate_);

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
  if (!payload_manager_.MaybeAllocatePayloadBufferForCopy(
          size, &copy_destination_buffer)) {
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
    // TODO(dalesat): Record/report packet drop.

    // We needed a packet and couldn't produce one, so we still need one.
    state_.store(State::kNeedsPacket);
    mate_->node()->NeedsUpdate();

    return nullptr;
  }

  // Copy the payload.
  FXL_DCHECK(copy_destination_buffer->size() >= size);
  memcpy(copy_destination_buffer->data(), packet->payload(), size);

  // Return a new packet like |packet| but with the new payload buffer.
  return CopyPacket(*packet, std::move(copy_destination_buffer));
}

void Input::RequestPacket() {
  FXL_DCHECK(mate_);

  State expected = State::kRefusesPacket;
  if (state_.compare_exchange_strong(expected, State::kNeedsPacket)) {
    mate_->node()->NeedsUpdate();
  }
}

void Input::Flush() { TakePacket(false); }

}  // namespace media_player
