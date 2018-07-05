// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <threads.h>

#include <lib/zx/timer.h>

#include <fbl/type_support.h>

#include <unistd.h>
#include <unittest/unittest.h>

static bool deadline_test() {
    BEGIN_TEST;
    auto then = zx_clock_get_monotonic();
    // The day we manage to boot and run this test in less than 1uS we need to fix this.
    ASSERT_GT(then, 1000u);

    auto one_hour_later = zx_deadline_after(ZX_HOUR(1));
    EXPECT_LT(then, one_hour_later);

    uint64_t too_big = UINT64_MAX - 100u;
    auto clamped = zx_deadline_after(too_big);
    EXPECT_EQ(clamped, ZX_TIME_INFINITE);
    END_TEST;
}

static bool basic_test() {
    BEGIN_TEST;
    zx::timer timer;
    ASSERT_EQ(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer), ZX_OK);

    zx_signals_t pending;
    EXPECT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, zx::time(), &pending), ZX_ERR_TIMED_OUT);
    EXPECT_EQ(pending, 0u);

    for (int ix = 0; ix != 10; ++ix) {
        const auto deadline_timer = zx::deadline_after(zx::msec(50));
        const auto deadline_wait = zx::deadline_after(zx::sec(1));
        // Timer should fire faster than the wait timeout.
        ASSERT_EQ(timer.set(deadline_timer, zx::nsec(0)), ZX_OK);

        EXPECT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, deadline_wait, &pending), ZX_OK);
        EXPECT_EQ(pending, ZX_TIMER_SIGNALED);
    }
    END_TEST;
}

static bool restart_test() {
    BEGIN_TEST;
    zx::timer timer;
    ASSERT_EQ(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer), ZX_OK);

    zx_signals_t pending;
    for (int ix = 0; ix != 10; ++ix) {
        const auto deadline_timer = zx::deadline_after(zx::msec(500));
        const auto deadline_wait = zx::deadline_after(zx::msec(1));
        // Setting a timer already running is equivalent to a cancel + set.
        ASSERT_EQ(timer.set(deadline_timer, zx::nsec(0)), ZX_OK);

        EXPECT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, deadline_wait, &pending), ZX_ERR_TIMED_OUT);
        EXPECT_EQ(pending, 0u);
    }
    END_TEST;
}

static bool invalid_calls() {
    BEGIN_TEST;

    zx::timer timer;
    ASSERT_EQ(zx::timer::create(0, ZX_CLOCK_UTC, &timer), ZX_ERR_INVALID_ARGS);
    ASSERT_EQ(zx::timer::create(ZX_TIMER_SLACK_LATE + 1, ZX_CLOCK_UTC, &timer), ZX_ERR_INVALID_ARGS);

    END_TEST;
}

static bool edge_cases() {
    BEGIN_TEST;

    zx::timer timer;
    ASSERT_EQ(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer), ZX_OK);
    ASSERT_EQ(timer.set(zx::time(), zx::nsec(0)), ZX_OK);

    END_TEST;
}

// furiously spin resetting the timer, trying to race with it going off to look for
// race conditions.
static bool restart_race() {
    BEGIN_TEST;

    const zx_time_t kTestDuration = ZX_SEC(5);
    auto start = zx_clock_get_monotonic();

    zx::timer timer;
    ASSERT_EQ(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer), ZX_OK);
    while (zx_clock_get_monotonic() - start < kTestDuration) {
        ASSERT_EQ(timer.set(zx::deadline_after(zx::usec(100)), zx::nsec(0)), ZX_OK);
    }

    EXPECT_EQ(timer.cancel(), ZX_OK);

    END_TEST;
}

// If the timer is already due at the moment it is started then the signal should be
// asserted immediately.  Likewise canceling the timer should immediately deassert
// the signal.
static bool signals_asserted_immediately() {
    BEGIN_TEST;
    zx::timer timer;
    ASSERT_EQ(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer), ZX_OK);

    for (int i = 0; i < 100; i++) {
        zx::time now = zx::clock::get<ZX_CLOCK_MONOTONIC>();

        EXPECT_EQ(timer.set(now, zx::nsec(0)), ZX_OK);

        zx_signals_t pending;
        EXPECT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, zx::time(), &pending), ZX_OK);
        EXPECT_EQ(pending, ZX_TIMER_SIGNALED);

        EXPECT_EQ(timer.cancel(), ZX_OK);

        EXPECT_EQ(timer.wait_one(ZX_TIMER_SIGNALED, zx::time(), &pending), ZX_ERR_TIMED_OUT);
        EXPECT_EQ(pending, 0u);
    }

    END_TEST;
}

// This test is disabled because is flaky. The system might have a timer
// nearby |deadline_1| or |deadline_2| and as such the test will fire
// either earlier or later than expected. The precise behavior is still
// tested by the "k timer tests" command.
//
// See ZX-1087 for the current owner.

static bool coalesce_test(uint32_t mode) {
    BEGIN_TEST;
    // The second timer will coalesce to the first one for ZX_TIMER_SLACK_LATE
    // but not for  ZX_TIMER_SLACK_EARLY. This test is not precise because the
    // system might have other timers that interfere with it. Precise tests are
    // avaliable as kernel tests.

    zx::timer timer_1;
    ASSERT_EQ(zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer_1), ZX_OK);
    zx::timer timer_2;
    ASSERT_EQ(zx::timer::create(mode, ZX_CLOCK_MONOTONIC, &timer_2), ZX_OK);

    zx_time_t start = zx_clock_get_monotonic();

    const auto deadline_1 = zx::time(start + ZX_MSEC(350));
    const auto deadline_2 = zx::time(start + ZX_MSEC(250));

    ASSERT_EQ(timer_1.set(deadline_1, zx::nsec(0)), ZX_OK);
    ASSERT_EQ(timer_2.set(deadline_2, zx::msec(110)), ZX_OK);

    zx_signals_t pending;
    EXPECT_EQ(timer_2.wait_one(ZX_TIMER_SIGNALED, zx::time::infinite(), &pending), ZX_OK);
    EXPECT_EQ(pending, ZX_TIMER_SIGNALED);

    auto duration = zx_clock_get_monotonic() - start;

    if (mode == ZX_TIMER_SLACK_LATE) {
        EXPECT_GE(duration, ZX_MSEC(350));
    } else if (mode == ZX_TIMER_SLACK_EARLY) {
        EXPECT_LE(duration, ZX_MSEC(345));
    } else {
        assert(false);
    }
    END_TEST;
}

// Test is disabled, see coalesce_test().
static bool coalesce_test_early() {
    return coalesce_test(ZX_TIMER_SLACK_EARLY);
}

// Test is disabled, see coalesce_test().
static bool coalesce_test_late() {
    return coalesce_test(ZX_TIMER_SLACK_LATE);
}

BEGIN_TEST_CASE(timers_test)
RUN_TEST(deadline_test)
RUN_TEST(invalid_calls)
RUN_TEST(basic_test)
// Disabled: RUN_TEST(coalesce_test_late)
// Disabled: RUN_TEST(coalesce_test_early)
RUN_TEST(restart_test)
RUN_TEST(edge_cases)
RUN_TEST(restart_race)
RUN_TEST(signals_asserted_immediately)
END_TEST_CASE(timers_test)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
