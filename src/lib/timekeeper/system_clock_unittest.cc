// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/timekeeper/system_clock.h"

#include <gtest/gtest.h>

namespace timekeeper {

namespace {

TEST(SystemClockTest, MonotonicClock) {
  SystemClock clock;

  auto time1 = clock.Now();
  auto time2 = clock.Now();

  EXPECT_GE(time2, time1);
}

TEST(SystemClockTest, UtcClock) {
  SystemClock clock;

  time_utc time1;
  ASSERT_EQ(ZX_OK, clock.UtcNow(&time1));

  EXPECT_GT(time1, time_utc(0));
}

}  // namespace
}  // namespace timekeeper
