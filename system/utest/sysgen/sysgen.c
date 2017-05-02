// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

static bool wrapper_test(void) {
    BEGIN_TEST;
    ASSERT_EQ(mx_syscall_test_wrapper(1, 2, 3), 6, "syscall_test_wrapper doesn't add up");
    ASSERT_EQ(mx_syscall_test_wrapper(-1, 2, 3), ERR_INVALID_ARGS, "vdso should have checked args");
    ASSERT_EQ(mx_syscall_test_wrapper(10, 20, 30), ERR_OUT_OF_RANGE, "vdso should have checked the return");
    END_TEST;
}

BEGIN_TEST_CASE(launchpad_tests)
RUN_TEST(wrapper_test);
END_TEST_CASE(launchpad_tests)

int main(int argc, char **argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
