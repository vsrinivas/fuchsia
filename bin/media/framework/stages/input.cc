// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>

#include "garnet/bin/media/framework/stages/input.h"

#include "garnet/bin/media/framework/engine.h"
#include "garnet/bin/media/framework/stages/stage_impl.h"

namespace media {

Input::Input(StageImpl* stage, size_t index)
    : stage_(stage), index_(index), state_(State::kRefusesPacket) {
  FXL_DCHECK(stage_);
}

Input::~Input() {}

void Input::Connect(Output* output) {
  FXL_DCHECK(output);
  FXL_DCHECK(!mate_);
  mate_ = output;
}

Demand Input::demand() const {
  State state = state_.load();
  switch (state) {
    case State::kDemandsPacket:
      return Demand::kPositive;
    case State::kAllowsPacket:
      return Demand::kNeutral;
    case State::kRefusesPacket:
    case State::kHasPacket:
      return Demand::kNegative;
  }
}

void Input::PutPacket(PacketPtr packet) {
  FXL_DCHECK(packet);
  FXL_DCHECK(demand() != Demand::kNegative);
  std::atomic_store(&packet_, packet);
  state_.store(State::kHasPacket);
  stage_->NeedsUpdate();
}

PacketPtr Input::TakePacket(Demand demand) {
  FXL_DCHECK(mate_);

  PacketPtr no_packet;
  PacketPtr packet = std::atomic_exchange(&packet_, no_packet);

  if (demand == Demand::kNegative) {
    state_.store(State::kRefusesPacket);
  } else {
    state_.store(demand == Demand::kPositive ? State::kDemandsPacket
                                             : State::kAllowsPacket);
    mate_->stage()->NeedsUpdate();
  }

  return packet;
}

void Input::SetDemand(Demand demand) {
  FXL_DCHECK(mate_);

  State state = state_.load();
  if (state == State::kHasPacket) {
    return;
  }

  State new_state;
  switch (demand) {
    case Demand::kPositive:
      new_state = State::kDemandsPacket;
      break;
    case Demand::kNeutral:
      new_state = State::kAllowsPacket;
      break;
    case Demand::kNegative:
      new_state = State::kRefusesPacket;
      break;
  }

  if (state != new_state) {
    FXL_DCHECK(new_state != State::kRefusesPacket);
    state_.store(new_state);
    mate_->stage()->NeedsUpdate();
  }
}

void Input::Flush() {
  TakePacket(Demand::kNegative);
}

}  // namespace media
