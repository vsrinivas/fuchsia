// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>

#include "garnet/bin/mediaplayer/framework/stages/input.h"

#include "garnet/bin/mediaplayer/framework/stages/stage_impl.h"

namespace media_player {

Input::Input(StageImpl* stage, size_t index)
    : stage_(stage), index_(index), state_(State::kRefusesPacket) {
  FXL_DCHECK(stage_);
}

Input::Input(Input&& input)
    : stage_(input.stage()),
      index_(input.index()),
      mate_(input.mate()),
      prepared_(input.prepared()),
      packet_(std::move(input.packet())),
      state_(input.state_.load()) {}

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
  stage_->NeedsUpdate();
}

PacketPtr Input::TakePacket(bool request_another) {
  FXL_DCHECK(mate_);

  PacketPtr no_packet;
  PacketPtr packet = std::atomic_exchange(&packet_, no_packet);

  if (request_another) {
    state_.store(State::kNeedsPacket);
    mate_->stage()->NeedsUpdate();
  } else {
    state_.store(State::kRefusesPacket);
  }

  return packet;
}

void Input::RequestPacket() {
  FXL_DCHECK(mate_);

  State expected = State::kRefusesPacket;
  if (state_.compare_exchange_strong(expected, State::kNeedsPacket)) {
    mate_->stage()->NeedsUpdate();
  }
}

void Input::Flush() { TakePacket(false); }

}  // namespace media_player
