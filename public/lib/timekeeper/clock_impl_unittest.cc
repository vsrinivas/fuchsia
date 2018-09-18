// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/timekeeper/clock_impl.h"

#include "gtest/gtest.h"

namespace timekeeper {

namespace {

TEST(ClockImplTest, MonotonicClock) {
  ClockImpl clock;

  auto time1 = clock.Now();
  auto time2 = clock.Now();

  EXPECT_GE(time2, time1);
}

TEST(ClockImplTest, UTCClock) {
  ClockImpl clock;

  zx::time_utc time1;
  ASSERT_EQ(ZX_OK, clock.Now(&time1));

  EXPECT_GT(time1, zx::time_utc(0));
}

TEST(ClockImplTest, ThreadClock) {
  ClockImpl clock;

  zx::time_thread time1, time2;
  ASSERT_EQ(ZX_OK, clock.Now(&time1));
  ASSERT_EQ(ZX_OK, clock.Now(&time2));

  EXPECT_GE(time2, time1);
}

}  // namespace
}  // namespace timekeeper
