// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stdio.h>
#include <magenta/compiler.h>

extern void thread_entry(uintptr_t arg);

int print_fail(void) {
    EXPECT_TRUE(false, "Failed");
    mx_thread_exit();
    return 1; // Not reached
}

// create a thread using the raw magenta api.
// cannot use a higher level api because they'll use trampoline functions that'll trash
// registers on entry.
mx_handle_t raw_thread_create(void (*thread_entry)(uintptr_t arg), uintptr_t arg)
{
    // preallocated stack to satisfy the thread we create
    static uint8_t stack[1024] __ALIGNED(16);

    mx_handle_t handle;
    mx_status_t status = mx_thread_create(mx_process_self(), "", 0, 0, &handle);
    if (status < 0)
        return status;

    status = mx_thread_start(handle, (uintptr_t)thread_entry,
                             (uintptr_t)stack + sizeof(stack),
                             arg, 0);
    if (status < 0) {
        mx_handle_close(handle);
        return status;
    }

    return handle;
}

bool tis_test(void) {
    BEGIN_TEST;
    uintptr_t arg = 0x1234567890abcdef;
    mx_handle_t handle = raw_thread_create(thread_entry, arg);
    ASSERT_GE(handle, 0, "Error while thread creation");

    mx_status_t status = mx_object_wait_one(handle, MX_THREAD_TERMINATED, MX_TIME_INFINITE, NULL);
    ASSERT_GE(status, 0, "Error while thread wait");
    END_TEST;
}

BEGIN_TEST_CASE(tis_tests)
RUN_TEST(tis_test)
END_TEST_CASE(tis_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
