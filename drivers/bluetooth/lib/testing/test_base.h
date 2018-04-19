// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "gtest/gtest.h"

#include <lib/async-testutils/test_loop.h>
#include <lib/async/cpp/task.h>
#include <zx/time.h>

#include "lib/fxl/macros.h"

namespace btlib {
namespace testing {

// Note: GTest uses the top-level "testing" namespace while the Bluetooth test
// utilities are in "::btlib::testing".
class TestBase : public ::testing::Test {
 public:
  TestBase() = default;
  virtual ~TestBase() = default;

 protected:
  // Pure-virtual overrides of ::testing::Test::SetUp() to force subclass
  // override.
  void SetUp() override = 0;

  // ::testing::Test override:
  void TearDown() override {}

  // Runs the message loop until it would wait.
  void RunUntilIdle() {
    loop_.ResetQuit();
    loop_.RunUntilIdle();
  }

  // Advances the fake clock by |delta|.
  void AdvanceTimeBy(zx::duration delta) { loop_.AdvanceTimeBy(delta); }

  // Getters for internal fields frequently used by tests.
  async_t* dispatcher() { return loop_.async(); }

 private:
  async::TestLoop loop_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestBase);
};

}  // namespace testing
}  // namespace btlib
