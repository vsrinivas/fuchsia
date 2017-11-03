// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "gtest/gtest.h"

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace testing {

// Note: GTest uses the top-level "testing" namespace while the Bluetooth test
// utilities are in "::bluetooth::testing".
class TestBase : public ::testing::Test {
 public:
  TestBase() = default;
  virtual ~TestBase() = default;

 protected:
  // Pure-virtual overrides of ::testing::Test::SetUp() to force subclass
  // override.
  void SetUp() override = 0;

  // ::testing::Test override:
  void TearDown() override  {}

  // Posts a delayed task to quit the message loop after |seconds| have elapsed.
  void PostDelayedQuitTask(const fxl::TimeDelta time_delta) {
    message_loop_.task_runner()->PostDelayedTask(
        [this] { message_loop_.QuitNow(); }, time_delta);
  }

  // Runs the message loop for the specified amount of time. This is useful for
  // callback-driven test cases in which the message loop may run forever if the
  // callback is not run.
  void RunMessageLoop(int64_t timeout_seconds = 10) {
    RunMessageLoop(fxl::TimeDelta::FromSeconds(timeout_seconds));
  }

  void RunMessageLoop(const fxl::TimeDelta& time_delta) {
    PostDelayedQuitTask(time_delta);
    message_loop_.Run();
  }

  // Getters for internal fields frequently used by tests.
  fsl::MessageLoop* message_loop() { return &message_loop_; }

 private:
  fsl::MessageLoop message_loop_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestBase);
};

}  // namespace testing
}  // namespace bluetooth
