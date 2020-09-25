// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/zx/clock.h>
#include <lib/zx/timer.h>
#include <stdio.h>
#include <threads.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

namespace {

void CheckInfo(const zx::timer& timer, uint32_t options, zx_time_t deadline, zx_duration_t slack) {
  zx_info_timer_t info = {};
  ASSERT_OK(timer.get_info(ZX_INFO_TIMER, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.options, options);
  EXPECT_EQ(info.deadline, deadline);
  EXPECT_EQ(info.slack, slack);
}

TEST(TimersTest, DeadlineAfter) {
  auto then = zx_clock_get_monotonic();
  // The day we manage to boot and run this test in less than 1uS we need to fix this.
  ASSERT_GT(then, 1000u);

  auto one_hour_later = zx_deadline_after(ZX_HOUR(1));
  EXPECT_LT(then, one_hour_later);

  zx_duration_t too_big = INT64_MAX - 100;
  auto clamped = zx_deadline_after(too_big);
  EXPECT_EQ(clamped, ZX_TIME_INFINITE);

  EXPECT_LT(0, zx_deadline_after(10 * 365 * ZX_HOUR(24)));
  EXPECT_LT(zx_deadline_after(ZX_TIME_INFINITE_PAST), 0);
}

TEST(TimersTest, SetNegativeDeadline) {
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));
  CheckInfo(timer, 0, 0, 0);
  zx::duration slack;
  ASSERT_OK(timer.set(zx::time(-1), slack));
  CheckInfo(timer, 0, 0, slack.get());
  zx_signals_t pending;
  ASSERT_OK(timer.wait_one(ZX_TIMER_SIGNALED, zx::time::infinite(), &pending));
  ASSERT_EQ(pending, ZX_TIMER_SIGNALED);
  CheckInfo(timer, 0, 0, 0);
}

TEST(TimersTest, SetNegativeDeadlineMax) {
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));
  zx::duration slack;
  ASSERT_OK(timer.set(zx::time(ZX_TIME_INFINITE_PAST), slack));
  CheckInfo(timer, 0, 0, slack.get());
  zx_signals_t pending;
  ASSERT_OK(timer.wait_one(ZX_TIMER_SIGNALED, zx::time::infinite(), &pending));
  ASSERT_EQ(pending, ZX_TIMER_SIGNALED);
  CheckInfo(timer, 0, 0, 0);
}

TEST(TimersTest, SetNegativeSlack) {
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));
  ASSERT_EQ(timer.set(zx::time(), zx::duration(-1)), ZX_ERR_OUT_OF_RANGE);
  CheckInfo(timer, 0, 0, 0);
}

TEST(TimersTest, AlreadyPassedDeadlineOnWaitOne) {
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));
  CheckInfo(timer, 0, 0, 0);

  zx::duration slack;
  ASSERT_OK(timer.set(zx::time(ZX_TIME_INFINITE_PAST), slack));
  CheckInfo(timer, 0, 0, slack.get());

  zx_signals_t pending;
  ASSERT_OK(timer.wait_one(ZX_TIMER_SIGNALED, zx::time::infinite_past(), &pending));
  ASSERT_EQ(pending, ZX_TIMER_SIGNALED);
  CheckInfo(timer, 0, 0, 0);
}

TEST(TimersTest, Basic) {
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));

  zx_signals_t pending;
  EXPECT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, zx::time(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending, 0u);

  for (int ix = 0; ix != 3; ++ix) {
    const auto deadline_timer = zx::deadline_after(zx::msec(10));
    const auto deadline_wait = zx::deadline_after(zx::sec(1000));
    // Timer should fire faster than the wait timeout.
    ASSERT_OK(timer.set(deadline_timer, zx::nsec(0)));

    EXPECT_OK(timer.wait_one(ZX_TIMER_SIGNALED, deadline_wait, &pending));
    EXPECT_EQ(pending, ZX_TIMER_SIGNALED);
    CheckInfo(timer, 0, 0, 0);
  }
}

TEST(TimersTest, Restart) {
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));

  zx_signals_t pending;
  for (int ix = 0; ix != 10; ++ix) {
    const auto deadline_timer = zx::deadline_after(zx::msec(500));
    const auto deadline_wait = zx::deadline_after(zx::msec(1));
    // Setting a timer already running is equivalent to a cancel + set.
    ASSERT_OK(timer.set(deadline_timer, zx::nsec(0)));
    CheckInfo(timer, 0, deadline_timer.get(), 0);

    EXPECT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, deadline_wait, &pending), ZX_ERR_TIMED_OUT);
    EXPECT_EQ(pending, 0u);
    CheckInfo(timer, 0, deadline_timer.get(), 0);
  }
}

TEST(TimersTest, InvalidCalls) {
  zx::timer timer;
  ASSERT_EQ(zx::timer::create(0, ZX_CLOCK_UTC, &timer), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(zx::timer::create(ZX_TIMER_SLACK_LATE + 1, ZX_CLOCK_UTC, &timer), ZX_ERR_INVALID_ARGS);
}

TEST(TimersTest, EdgeCases) {
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));
  ASSERT_OK(timer.set(zx::time(), zx::nsec(0)));
}

// furiously spin resetting the timer, trying to race with it going off to look for
// race conditions.
TEST(TimersTest, RestartRace) {
  const zx_time_t kTestDuration = ZX_SEC(5);
  auto start = zx_clock_get_monotonic();

  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));
  while (zx_clock_get_monotonic() - start < kTestDuration) {
    ASSERT_OK(timer.set(zx::deadline_after(zx::usec(100)), zx::nsec(0)));
  }

  EXPECT_OK(timer.cancel());
}

// If the timer is already due at the moment it is started then the signal should be
// asserted immediately.  Likewise canceling the timer should immediately deassert
// the signal.
TEST(TimersTest, SignalsAssertedImmediately) {
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer));

  for (int i = 0; i < 100; i++) {
    zx::time now = zx::clock::get_monotonic();

    EXPECT_OK(timer.set(now, zx::nsec(0)));

    zx_signals_t pending;
    EXPECT_OK(timer.wait_one(ZX_TIMER_SIGNALED, zx::time(), &pending));
    EXPECT_EQ(pending, ZX_TIMER_SIGNALED);

    EXPECT_OK(timer.cancel());

    EXPECT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, zx::time(), &pending), ZX_ERR_TIMED_OUT);
    EXPECT_EQ(pending, 0u);
  }
}

// Tests using CheckCoalescing are disabled because they are flaky. The system might have a timer
// nearby |deadline_1| or |deadline_2| and as such the test will fire either earlier or later than
// expected. The precise behavior is still tested by the "k timer tests" command.
//
// See fxbug.dev/31030 for the current owner.
void CheckCoalescing(uint32_t mode) {
  // The second timer will coalesce to the first one for ZX_TIMER_SLACK_LATE
  // but not for  ZX_TIMER_SLACK_EARLY. This test is not precise because the
  // system might have other timers that interfere with it. Precise tests are
  // avaliable as kernel tests.

  zx::timer timer_1;
  ASSERT_OK(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer_1));
  zx::timer timer_2;
  ASSERT_OK(zx::timer::create(mode, ZX_CLOCK_MONOTONIC, &timer_2));

  zx_time_t start = zx_clock_get_monotonic();

  const auto deadline_1 = zx::time(start + ZX_MSEC(350));
  const auto deadline_2 = zx::time(start + ZX_MSEC(250));

  ASSERT_OK(timer_1.set(deadline_1, zx::nsec(0)));
  ASSERT_OK(timer_2.set(deadline_2, zx::msec(110)));
  CheckInfo(timer_2, 0, deadline_2.get(), ZX_MSEC(110));

  zx_signals_t pending;
  EXPECT_OK(timer_2.wait_one(ZX_TIMER_SIGNALED, zx::time::infinite(), &pending));
  EXPECT_EQ(pending, ZX_TIMER_SIGNALED);
  CheckInfo(timer_2, 0, 0, 0);

  auto duration = zx_clock_get_monotonic() - start;

  if (mode == ZX_TIMER_SLACK_LATE) {
    EXPECT_GE(duration, ZX_MSEC(350));
  } else if (mode == ZX_TIMER_SLACK_EARLY) {
    EXPECT_LE(duration, ZX_MSEC(345));
  } else {
    assert(false);
  }
}

// Test is disabled, see |CheckCoalescing|.
TEST(TimersTest, DISABLED_CoalesceTestEarly) {
  ASSERT_NO_FAILURES(CheckCoalescing(ZX_TIMER_SLACK_EARLY));
}

// Test is disabled, see |CheckCoalescing|.
TEST(TimersTest, DISABLED_CoalesceTestLate) {
  ASSERT_NO_FAILURES(CheckCoalescing(ZX_TIMER_SLACK_LATE));
}

}  // anonymous namespace
