// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <threads.h>

#include <mx/channel.h>
#include <mx/event.h>
#include <mx/handle.h>
#include <mx/port.h>

#include <mxtl/type_support.h>

#include <unistd.h>
#include <unittest/unittest.h>

static bool basic_test() {
    BEGIN_TEST;
    mx::handle timer;
    ASSERT_EQ(mx_timer_create(0, MX_CLOCK_MONOTONIC, timer.get_address()), MX_OK, "");

    mx_signals_t pending;
    EXPECT_EQ(timer.wait_one(MX_TIMER_SIGNALED, 0u, &pending), MX_ERR_TIMED_OUT, "");
    EXPECT_EQ(pending, MX_SIGNAL_LAST_HANDLE, "");

    for (int ix = 0; ix != 10; ++ix) {
        const auto deadline_timer = mx_deadline_after(MX_MSEC(50));
        const auto deadline_wait = mx_deadline_after(MX_SEC(1));
        // Timer should fire faster than the wait timeout.
        ASSERT_EQ(mx_timer_start(timer.get(), deadline_timer, 0u, 0u), MX_OK, "");
        EXPECT_EQ(timer.wait_one(MX_TIMER_SIGNALED, deadline_wait, &pending), MX_OK, "");
        EXPECT_EQ(pending, MX_TIMER_SIGNALED | MX_SIGNAL_LAST_HANDLE, "");
    }
    END_TEST;
}

static bool restart_test() {
    BEGIN_TEST;
    mx::handle timer;
    ASSERT_EQ(mx_timer_create(0, MX_CLOCK_MONOTONIC, timer.get_address()), MX_OK, "");

    mx_signals_t pending;
    for (int ix = 0; ix != 10; ++ix) {
        const auto deadline_timer = mx_deadline_after(MX_MSEC(500));
        const auto deadline_wait = mx_deadline_after(MX_MSEC(1));
        // Setting a timer already running is equivalent to a cancel + set.
        ASSERT_EQ(mx_timer_start(timer.get(), deadline_timer, 0u, 0u), MX_OK, "");
        EXPECT_EQ(timer.wait_one(MX_TIMER_SIGNALED, deadline_wait, &pending), MX_ERR_TIMED_OUT, "");
        EXPECT_EQ(pending, MX_SIGNAL_LAST_HANDLE, "");
    }
    END_TEST;
}

static bool invalid_calls() {
    BEGIN_TEST;

    mx::handle timer;
    ASSERT_EQ(mx_timer_create(0, MX_CLOCK_UTC, timer.get_address()), MX_ERR_INVALID_ARGS, "");
    ASSERT_EQ(mx_timer_create(1, MX_CLOCK_MONOTONIC, timer.get_address()), MX_ERR_INVALID_ARGS, "");

    ASSERT_EQ(mx_timer_create(0, MX_CLOCK_MONOTONIC, timer.get_address()), MX_OK, "");
    ASSERT_EQ(mx_timer_start(timer.get(), 0u, 0u, 0u), MX_ERR_INVALID_ARGS, "");
    ASSERT_EQ(mx_timer_start(timer.get(), MX_TIMER_MIN_DEADLINE - 1, 0u, 0u), MX_ERR_INVALID_ARGS, "");

    const auto deadline_timer = mx_deadline_after(MX_MSEC(1));
    ASSERT_EQ(mx_timer_start(timer.get(), deadline_timer, MX_USEC(2), 0u), MX_ERR_NOT_SUPPORTED, "");

    END_TEST;
}

static bool edge_cases() {
    BEGIN_TEST;

    mx::handle timer;
    ASSERT_EQ(mx_timer_create(0, MX_CLOCK_MONOTONIC, timer.get_address()), MX_OK, "");
    ASSERT_EQ(mx_timer_start(timer.get(), MX_TIMER_MIN_DEADLINE, 0u, 0u), MX_OK, "");
    ASSERT_EQ(mx_timer_start(timer.get(), MX_TIMER_MIN_DEADLINE, MX_TIMER_MIN_PERIOD, 0u), MX_OK, "");

    END_TEST;
}

static bool periodic() {
    BEGIN_TEST;

    mx::handle timer;
    ASSERT_EQ(mx_timer_create(0, MX_CLOCK_MONOTONIC, timer.get_address()), MX_OK, "");

    const auto deadline_timer = mx_deadline_after(MX_MSEC(1));
    const auto period = MX_USEC(500);

    ASSERT_EQ(mx_timer_start(timer.get(), deadline_timer, period, 0u), MX_OK, "");

    mx_signals_t pending;
    auto expected_arrival = deadline_timer;

    for (int ix = 0; ix != 100; ++ix) {
        EXPECT_EQ(timer.wait_one(MX_TIMER_SIGNALED, MX_TIME_INFINITE, &pending), MX_OK, "");
        EXPECT_EQ(pending & MX_TIMER_SIGNALED, MX_TIMER_SIGNALED, "");

        EXPECT_GT(mx_time_get(MX_CLOCK_MONOTONIC), expected_arrival, "");
        expected_arrival += period;
    }

    EXPECT_EQ(mx_timer_cancel(timer.get()), MX_OK, "");
    END_TEST;
}

BEGIN_TEST_CASE(timers_test)
RUN_TEST(basic_test)
RUN_TEST(restart_test)
RUN_TEST(invalid_calls)
RUN_TEST(edge_cases)
RUN_TEST(periodic)
END_TEST_CASE(timers_test)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
