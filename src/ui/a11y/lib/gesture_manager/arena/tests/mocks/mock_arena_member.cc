// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_arena_member.h"

#include <src/lib/fxl/logging.h>

#include "gtest/gtest.h"

namespace accessibility_test {

MockArenaMember::MockArenaMember(a11y::GestureRecognizer* recognizer)
    : a11y::ArenaMember(&arena_, &router_, recognizer) {
  recognizer_ = recognizer;
}

bool MockArenaMember::DeclareDefeat() {
  declare_defeat_called_ = true;
  EXPECT_FALSE(IsOnWinCalled());
  recognizer_->OnDefeat();
  return true;
}

bool MockArenaMember::StopRoutingPointerEvents(
    fuchsia::ui::input::accessibility::EventHandling handled) {
  stop_routing_pointer_events_called_ = true;
  return true;
}

bool MockArenaMember::CallOnWin() {
  recognizer_->OnWin();
  on_win_called_ = true;

  return true;
}

}  // namespace accessibility_test
