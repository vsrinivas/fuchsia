// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <unittest/unittest.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

bool normal_math_test(void) {
    BEGIN_TEST;

    int a = 5;
    int b = atoi("6"); // avoid compiler optimizations
    int c = a+b;
    EXPECT_EQ(11, c, "The world is broken");

    END_TEST;
}

bool signed_overflow_test(void) {
    BEGIN_TEST;

    ASSERT_DEATH([](void*) {
        int a = INT_MAX;
        int b = atoi("6"); // avoid compiler optimizations
        int c = a+b;       // crash occurs here
        printf("%d\n", c); // should not be reached.
    }, 0, "overflow should have caused a crash");

    END_TEST;
}

bool signed_underflow_test(void) {
    BEGIN_TEST;

    ASSERT_DEATH([](void*) {
        int a = INT_MIN;
        int b = atoi("-6"); // avoid compiler optimizations
        int c = a+b;       // crash occurs here
        printf("%d\n", c); // should not be reached.
    }, 0, "underflow should have caused a crash");

    END_TEST;
}

bool divide_by_zero_test(void) {
    BEGIN_TEST;

    ASSERT_DEATH([](void*) {
        int a = 5;
        int b = atoi("0"); // avoid compiler optimizations
        int c = a/b;       // crash occurs here
        printf("%d\n", c); // should not be reached.
    }, 0, "divide by zero should have caused a crash");

    END_TEST;
}

BEGIN_TEST_CASE(integer_tests)
RUN_TEST(normal_math_test)
RUN_TEST_ENABLE_CRASH_HANDLER(signed_overflow_test)
RUN_TEST_ENABLE_CRASH_HANDLER(signed_underflow_test)
RUN_TEST_ENABLE_CRASH_HANDLER(divide_by_zero_test)
END_TEST_CASE(integer_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
