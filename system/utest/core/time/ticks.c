// Copyright 2016 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

// mx_get_ticks() should return ticks which monotonically increase.
static bool ticks_increase_monotonically(void) {
    BEGIN_TEST;

    uint64_t prev = 0;
    for (int i = 0; i < 100; i++) {
        uint64_t ticks = mx_ticks_get();
        ASSERT_LE(prev, ticks, "");
        prev = ticks;
    }

    END_TEST;
}

BEGIN_TEST_CASE(ticks_tests)
RUN_TEST(ticks_increase_monotonically)
END_TEST_CASE(ticks_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
