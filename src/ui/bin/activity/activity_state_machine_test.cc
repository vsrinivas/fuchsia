// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/activity/activity_state_machine.h"

#include <fuchsia/ui/activity/cpp/fidl.h>

#include <gtest/gtest.h>

namespace activity {

TEST(ActivityStateMachine, BaseStateIdle) {
  ActivityStateMachine state_machine;
  EXPECT_EQ(state_machine.state(), fuchsia::ui::activity::State::IDLE);
}

TEST(ActivityStateMachine, ActiveToInactive) {
  ActivityStateMachine state_machine;
  state_machine.ReceiveEvent(Event::USER_INPUT);
  EXPECT_EQ(state_machine.state(), fuchsia::ui::activity::State::ACTIVE);

  // Subsequent events remain ACTIVE
  state_machine.ReceiveEvent(Event::USER_INPUT);
  EXPECT_EQ(state_machine.state(), fuchsia::ui::activity::State::ACTIVE);

  state_machine.ReceiveEvent(Event::TIMEOUT);
  EXPECT_EQ(state_machine.state(), fuchsia::ui::activity::State::IDLE);

  // Timeouts are ignored while IDLE
  state_machine.ReceiveEvent(Event::TIMEOUT);
  EXPECT_EQ(state_machine.state(), fuchsia::ui::activity::State::IDLE);
}

}  // namespace activity
