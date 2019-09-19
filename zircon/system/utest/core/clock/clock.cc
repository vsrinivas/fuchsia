// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/clock.h>
#include <lib/zx/time.h>

#include <array>
#include <utility>

#include <zxtest/zxtest.h>

namespace {

constexpr zx::duration k1MsDelay = zx::msec(1);

TEST(ClockTest, ClockMonotonic) {
  const zx::time zero;
  zx::time previous = zx::clock::get_monotonic();

  for (int idx = 0; idx < 100; ++idx) {
    zx::time current = zx::clock::get_monotonic();
    ASSERT_GT(current, zero, "monotonic time should be a positive number of nanoseconds");
    ASSERT_GE(current, previous, "monotonic time should only advance");
    // This calls zx_nanosleep directly rather than using
    // zx_deadline_after, which internally gets the monotonic
    // clock.
    zx::nanosleep(current + k1MsDelay);

    previous = current;
  }
}

TEST(ClockTest, DeadlineAfter) {
  constexpr std::array Offsets = {ZX_MSEC(0), ZX_MSEC(20)};

  // Make sure that zx_deadline_after always gives results which are consistent
  // with simply getting clock monotonic and adding our own offset.
  for (auto offset : Offsets) {
    zx_time_t before, after, deadline;

    before = zx_time_add_duration(zx_clock_get_monotonic(), offset);
    deadline = zx_deadline_after(offset);
    after = zx_time_add_duration(zx_clock_get_monotonic(), offset);

    ASSERT_GE(deadline, before);
    ASSERT_LE(deadline, after);
  }
}

}  // namespace
