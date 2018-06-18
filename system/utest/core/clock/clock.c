// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>

#include <zircon/syscalls.h>
#include <unittest/unittest.h>

static bool clock_monotonic_test(void) {
    BEGIN_TEST;

    const zx_time_t zero = 0;

    zx_time_t previous = zx_clock_get_monotonic();

    for (int idx = 0; idx < 100; ++idx) {
        zx_time_t current = zx_clock_get_monotonic();
        ASSERT_GT(current, zero, "monotonic time should be a positive number of nanoseconds");
        ASSERT_GE(current, previous, "monotonic time should only advance");

        // This calls zx_nanosleep directly rather than using
        // zx_deadline_after, which internally gets the monotonic
        // clock.
        zx_nanosleep(current + 1000u);

        previous = current;
    }

    END_TEST;
}

BEGIN_TEST_CASE(clock_tests)
RUN_TEST(clock_monotonic_test)
END_TEST_CASE(clock_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
