// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/timekeeper/async_test_clock.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/time.h>

#include "gtest/gtest.h"

namespace timekeeper {
namespace {

TEST(AsyncTestClockTest, Increment) {
  async::TestLoop loop;
  AsyncTestClock clock(loop.dispatcher());

  auto time1 = clock.Now();

  EXPECT_EQ(async::Now(loop.dispatcher()), time1);

  auto time2 = clock.Now();
  EXPECT_GT(time2, time1);
}

}  // namespace
}  // namespace timekeeper
