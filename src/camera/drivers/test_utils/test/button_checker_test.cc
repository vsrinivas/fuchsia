// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../button_checker.h"

#include <iostream>

#include <gtest/gtest.h>

TEST(ButtonCheckerTest, CheckMuteState) {
  auto checker = ButtonChecker::Create();
  if (!checker) {
    std::cerr << "ButtonChecker not created. Board may lack input devices." << std::endl;
    // This is not a failure, but the test cannot meaningfully continue.
  } else {
    // TODO(37896): Not exactly the same as zxtest's ASSERT_NO_DEATH.
    ASSERT_NO_FATAL_FAILURE( checker->GetMuteState() );
  }
}
