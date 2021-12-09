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

TEST(SyncWaitTest, WaitFor) {
  SyncWait sync;
  sync.set_threshold(zx::msec(1));
  EXPECT_FALSE(sync.is_signaled());
  EXPECT_FALSE(sync.has_exceeded_threshold());

  std::thread t([&]() { sync.WaitFor("`Signal` to be called"); });
  zx::nanosleep(zx::deadline_after(zx::msec(5)));
  EXPECT_FALSE(sync.is_signaled());

  sync.Signal();
  t.join();
  EXPECT_TRUE(sync.is_signaled());
  EXPECT_TRUE(sync.has_exceeded_threshold());
}

TEST(SyncWaitTest, TimedWait) {
  SyncWait sync;
  EXPECT_EQ(sync.TimedWait(zx::msec(1)), ZX_ERR_TIMED_OUT);
  sync.Signal();
  EXPECT_EQ(sync.TimedWait(zx::msec(1)), ZX_OK);
}

TEST(SyncWaitTest, WaitUntil) {
  SyncWait sync;
  auto now = zx::clock::get_monotonic();
  EXPECT_EQ(sync.WaitUntil(now), ZX_ERR_TIMED_OUT);
  sync.Signal();
  EXPECT_EQ(sync.WaitUntil(now), ZX_OK);
}

TEST(SyncWaitTest, Reset) {
  SyncWait sync;
  sync.set_threshold(zx::usec(1));
  EXPECT_FALSE(sync.is_signaled());
  EXPECT_FALSE(sync.has_exceeded_threshold());

  sync.Signal();
  EXPECT_TRUE(sync.is_signaled());
  EXPECT_FALSE(sync.has_exceeded_threshold());

  sync.Reset();
  EXPECT_FALSE(sync.is_signaled());
  EXPECT_FALSE(sync.has_exceeded_threshold());

  std::thread t([&]() { sync.WaitFor("`Signal` to be called again"); });
  zx::nanosleep(zx::deadline_after(zx::msec(5)));
  EXPECT_FALSE(sync.is_signaled());

  sync.Signal();
  t.join();
  EXPECT_TRUE(sync.is_signaled());
  EXPECT_TRUE(sync.has_exceeded_threshold());

  sync.Reset();
  EXPECT_FALSE(sync.is_signaled());
  EXPECT_FALSE(sync.has_exceeded_threshold());
}

}  // namespace fuzzing
