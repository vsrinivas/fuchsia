// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unittest/unittest.h>

#include "private.h"

int thread_injection_test(void) {
    BEGIN_TEST;

    char msg[128];

    // Create a message pipe to communicate with the injector.  This
    // pipe will serve two purposes.  First, we'll use it to give the
    // injector some important bits and our process handle.  Second,
    // it will serve as the bootstrap pipe for the injected program.
    // There is no facility for the injector to inject a handle into
    // another process, so it relies on us (the injectee) having
    // created the pipe beforehand and told the injector its handle
    // number in this process.
    mx_handle_t pipeh[2];
    mx_status_t status = mx_msgpipe_create(pipeh, 0);
    snprintf(msg, sizeof(msg), "mx_msgpipe_create failed: %d", status);
    ASSERT_EQ(status, 0, msg);

    // Start the injector program, which will inject a third program
    // into this here process.
    const char* argv[] = { "/boot/bin/thread-injection-injector" };
    uint32_t id = MX_HND_INFO(MX_HND_TYPE_USER0, 0);
    mx_handle_t proc = launchpad_launch_mxio_etc(argv[0], 1, argv, NULL,
                                                 1, &pipeh[1], &id);
    snprintf(msg, sizeof(msg), "launchpad_launch_mxio_etc failed: %d", proc);
    ASSERT_GT(proc, 0, msg);
    mx_handle_close(proc);

    // Now send our own process handle to the injector, along with
    // some crucial information.
    atomic_int my_futex = ATOMIC_VAR_INIT(0);
    struct helper_data data = {
        .futex_addr = &my_futex,
        .bootstrap = pipeh[0],
    };
    proc = mx_handle_duplicate(mx_process_self(), MX_RIGHT_SAME_RIGHTS);
    snprintf(msg, sizeof(msg), "mx_handle_duplicate failed on %#x: %d",
             mx_process_self(), status);
    ASSERT_GT(proc, 0, msg);

    status = mx_msgpipe_write(pipeh[0], &data, sizeof(data), &proc, 1, 0);
    snprintf(msg, sizeof(msg), "mx_msgpipe_write failed: %d", status);
    ASSERT_EQ(status, 0, msg);

    // Now the injector will inject the "injected" program into this process.
    // When that program starts up, it will see the &my_futex value and
    // do a store of the magic value and a mx_futex_wake operation.
    // When it's done that, the test has succeeded.
    while (atomic_load(&my_futex) == 0) {
        status = mx_futex_wait(&my_futex, 0, MX_TIME_INFINITE);
        snprintf(msg, sizeof(msg), "mx_futex_wait failed: %d", status);
        ASSERT_EQ(status, 0, msg);
    }
    snprintf(msg, sizeof(msg), "futex set to %#x", my_futex);
    ASSERT_EQ(my_futex, MAGIC, msg);

    END_TEST;
}

BEGIN_TEST_CASE(thread_injection_tests)
RUN_TEST(thread_injection_test)
END_TEST_CASE(thread_injection_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
