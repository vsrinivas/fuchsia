// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena/tests/mocks/mock_arena_member.h"

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"

namespace accessibility_test {

MockArenaMember::MockArenaMember(a11y::GestureRecognizer* recognizer)
    : a11y::ArenaMember(&arena_, recognizer) {
  recognizer_ = recognizer;
}

void MockArenaMember::Reject() {
  reject_called_ = true;
  recognizer_->OnDefeat();
}

bool MockArenaMember::CallOnWin() {
  recognizer_->OnWin();
  on_win_called_ = true;
  return true;
}

}  // namespace accessibility_test
