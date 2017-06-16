// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

static bool wrapper_test(void) {
    BEGIN_TEST;
    ASSERT_EQ(mx_syscall_test_wrapper(1, 2, 3), 6, "syscall_test_wrapper doesn't add up");
    ASSERT_EQ(mx_syscall_test_wrapper(-1, 2, 3), MX_ERR_INVALID_ARGS, "vdso should have checked args");
    ASSERT_EQ(mx_syscall_test_wrapper(10, 20, 30), MX_ERR_OUT_OF_RANGE, "vdso should have checked the return");
    END_TEST;
}

static bool syscall_test(void) {
    BEGIN_TEST;
    ASSERT_EQ(mx_syscall_test_8(1, 2, 3, 4, 5, 6, 7, 8), 36, "syscall8_test doesn't add up");
    ASSERT_EQ(mx_syscall_test_7(1, 2, 3, 4, 5, 6, 7), 28, "syscall7_test doesn't add up");
    ASSERT_EQ(mx_syscall_test_6(1, 2, 3, 4, 5, 6), 21, "syscall6_test doesn't add up");
    ASSERT_EQ(mx_syscall_test_5(1, 2, 3, 4, 5), 15, "syscall5_test doesn't add up");
    ASSERT_EQ(mx_syscall_test_4(1, 2, 3, 4), 10, "syscall4_test doesn't add up");
    ASSERT_EQ(mx_syscall_test_3(1, 2, 3), 6, "syscall3_test doesn't add up");
    ASSERT_EQ(mx_syscall_test_2(1, 2), 3, "syscall2_test doesn't add up");
    ASSERT_EQ(mx_syscall_test_1(1), 1, "syscall1_test doesn't add up");
    ASSERT_EQ(mx_syscall_test_0(), 0, "syscall0_test doesn't add up");
    END_TEST;
}

BEGIN_TEST_CASE(launchpad_tests)
RUN_TEST(wrapper_test);
RUN_TEST(syscall_test);
END_TEST_CASE(launchpad_tests)

int main(int argc, char **argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
