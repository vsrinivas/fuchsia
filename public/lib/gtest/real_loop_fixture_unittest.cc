// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gtest/real_loop_fixture.h"

#include <lib/async/cpp/task.h>

namespace gtest {
namespace {

using RealLoopFixtureTest = RealLoopFixture;

TEST_F(RealLoopFixtureTest, Timeout) {
  bool called = false;
  async::PostDelayedTask([&called] { called = true; }, zx::msec(100));
  RunLoopWithTimeout(zx::msec(10));
  EXPECT_FALSE(called);
  RunLoopWithTimeout(zx::msec(100));
  EXPECT_TRUE(called);
}

TEST_F(RealLoopFixtureTest, NoTimeout) {
  QuitLoop();
  // Check that the first run loop doesn't hit the timeout.
  EXPECT_FALSE(RunLoopWithTimeout(zx::msec(10)));
  // But the second does.
  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(10)));
}

}  // namespace
}  // namespace gtest
