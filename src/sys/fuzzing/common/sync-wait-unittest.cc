// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/sync-wait.h"

#include <lib/zx/clock.h>
#include <lib/zx/time.h>

#include <memory>
#include <thread>

#include <gtest/gtest.h>

namespace fuzzing {

class SyncWaitTest : public ::testing::Test {
 protected:
  void SetUp() override { SetThreshold(zx::msec(1)); }
  void TearDown() override { ResetThreshold(); }
};

TEST_F(SyncWaitTest, WaitFor) {
  SyncWait sync;
  EXPECT_FALSE(sync.is_signaled());

  std::thread t([&]() { sync.WaitFor("`Signal` to be called"); });
  zx::nanosleep(zx::deadline_after(zx::msec(5)));
  EXPECT_FALSE(sync.is_signaled());

  sync.Signal();
  t.join();
  EXPECT_TRUE(sync.is_signaled());
}

TEST_F(SyncWaitTest, TimedWait) {
  SyncWait sync;
  EXPECT_EQ(sync.TimedWait(zx::msec(1)), ZX_ERR_TIMED_OUT);
  sync.Signal();
  EXPECT_EQ(sync.TimedWait(zx::msec(1)), ZX_OK);
}

TEST_F(SyncWaitTest, WaitUntil) {
  SyncWait sync;
  auto now = zx::clock::get_monotonic();
  EXPECT_EQ(sync.WaitUntil(now), ZX_ERR_TIMED_OUT);
  sync.Signal();
  EXPECT_EQ(sync.WaitUntil(now), ZX_OK);
}

TEST_F(SyncWaitTest, Reset) {
  SyncWait sync;
  EXPECT_FALSE(sync.is_signaled());

  sync.Signal();
  EXPECT_TRUE(sync.is_signaled());

  sync.Reset();
  EXPECT_FALSE(sync.is_signaled());

  std::thread t([&]() { sync.WaitFor("`Signal` to be called again"); });
  zx::nanosleep(zx::deadline_after(zx::msec(5)));
  EXPECT_FALSE(sync.is_signaled());

  sync.Signal();
  t.join();
  EXPECT_TRUE(sync.is_signaled());

  sync.Reset();
  EXPECT_FALSE(sync.is_signaled());
}

}  // namespace fuzzing
