// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/test_utils/button_checker.h"

#include <iostream>

#include <gtest/gtest.h>

TEST(ButtonCheckerTest, CheckMuteState) {
  auto checker = ButtonChecker::Create();
  ASSERT_NE(checker, nullptr) << "ButtonChecker not created. This test should only be run in "
                                 "environments with mute buttons.";
  auto state = checker->GetMuteState();
  ASSERT_FALSE(HasFatalFailure());
  switch (state) {
    case ButtonChecker::ButtonState::DOWN:
      std::cerr << "Device Muted" << std::endl;
      break;
    case ButtonChecker::ButtonState::UP:
      std::cerr << "Device Unmuted" << std::endl;
      break;
    case ButtonChecker::ButtonState::UNKNOWN:
      std::cerr << "Device Mute State Unknown" << std::endl;
      break;
    default:
      ADD_FAILURE() << "Unexpected Mute State " << static_cast<uint32_t>(state);
  }
  std::cerr.flush();
}
