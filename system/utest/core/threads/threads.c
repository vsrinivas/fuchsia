// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <runtime/thread.h>

static void test_thread_fn(void* arg) {
    // Note: You shouldn't use C standard library functions from this thread.
    mx_nanosleep(MX_MSEC(100));
    mx_thread_exit();
}

bool threads_test(void) {
    BEGIN_TEST;

    const mx_size_t stack_size = 256u << 10;
    mx_handle_t thread_stack_vmo = mx_vmo_create(stack_size);
    ASSERT_GT(thread_stack_vmo, 0, "");

    uintptr_t stack = 0u;
    ASSERT_EQ(mx_process_map_vm(mx_process_self(), thread_stack_vmo, 0, stack_size, &stack,
                                MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(thread_stack_vmo), NO_ERROR, "");

    mxr_thread_t* thread = NULL;
    ASSERT_EQ(mxr_thread_create("test_thread", &thread), NO_ERROR, "");
    ASSERT_EQ(mxr_thread_start(thread, stack, stack_size, test_thread_fn, NULL), NO_ERROR, "");

    ASSERT_EQ(mx_handle_wait_one(mxr_thread_get_handle(thread), MX_SIGNAL_SIGNALED,
                                 MX_TIME_INFINITE, NULL), NO_ERROR, "");

    mxr_thread_destroy(thread);

    // Creating a thread with a super long name should fail.
    thread = NULL;
    EXPECT_LT(mxr_thread_create(
        "01234567890123456789012345678901234567890123456789012345678901234567890123456789",
        &thread), 0, "Thread creation should have failed (name too long)");

    END_TEST;
}

BEGIN_TEST_CASE(threads_tests)
RUN_TEST(threads_test)
END_TEST_CASE(threads_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
