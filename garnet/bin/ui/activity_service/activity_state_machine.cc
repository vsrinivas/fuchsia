// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/activity_service/activity_state_machine.h"

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/zx/time.h>

#include <optional>

namespace activity_service {

namespace fua = fuchsia::ui::activity;

// TODO(jfsulliv): Handle other input types (e.g. lid close/open)
const ActivityStateMachine::StateTable ActivityStateMachine::kStateTable{
    {{fua::State::IDLE, activity_service::Event::USER_INPUT}, fua::State::ACTIVE},
    {{fua::State::ACTIVE, activity_service::Event::TIMEOUT}, fua::State::IDLE},
};

// TODO(jfsulliv): Make this configurable.
const zx::duration ActivityStateMachine::kIdleDuration = zx::min(15);

void ActivityStateMachine::ReceiveEvent(Event event) {
  const auto& next_state = kStateTable.find(std::make_pair(state_, event));
  if (next_state != kStateTable.end()) {
    state_ = next_state->second;
  }
}

std::optional<zx::duration> ActivityStateMachine::TimeoutFor(
    const fuchsia::ui::activity::State state) {
  switch (state) {
    case fuchsia::ui::activity::State::ACTIVE:
      return kIdleDuration;
    default:
      return std::nullopt;
  }
}

}  // namespace activity_service
