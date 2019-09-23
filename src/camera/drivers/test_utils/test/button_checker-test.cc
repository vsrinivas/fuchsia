// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../button_checker.h"

#include <iostream>

#include <zxtest/zxtest.h>

TEST(ButtonCheckerTest, CheckMuteState) {
  auto checker = ButtonChecker::Create();
  if (!checker) {
    std::cerr << "ButtonChecker not created. Board may lack input devices." << std::endl;
    // This is not a failure, but the test cannot meaningfully continue.
    return;
  }
  ASSERT_NO_DEATH([&]() { checker->GetMuteState(); });
}
