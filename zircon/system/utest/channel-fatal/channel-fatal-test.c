// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <unittest/unittest.h>

static const char* process_bin;

// SYSCALL_zx_channel_call_noretry is an internal system call used in the
// vDSO's implementation of zx_channel_call.  It's not part of the ABI and
// so it's not exported from the vDSO.  It's hard to test the kernel's
// invariants without calling this directly.  So use some chicanery to
// find its address in the vDSO despite it not being public.
//
// The vdso-code.h header file is generated from the vDSO binary.  It gives
// the offsets of the internal functions.  So take a public vDSO function,
// subtract its offset to discover the vDSO base (could do this other ways,
// but this is the simplest), and then add the offset of the internal
// SYSCALL_zx_channel_call_noretry function we want to call.
#include "vdso-code.h"
static zx_status_t zx_channel_call_noretry(zx_handle_t handle,
                                           uint32_t options,
                                           zx_time_t deadline,
                                           const zx_channel_call_args_t* args,
                                           uint32_t* actual_bytes,
                                           uint32_t* actual_handles) {
    uintptr_t vdso_base =
        (uintptr_t)&zx_handle_close - VDSO_SYSCALL_zx_handle_close;
    uintptr_t fnptr = vdso_base + VDSO_SYSCALL_zx_channel_call_noretry;
    return (*(__typeof(zx_channel_call_noretry)*)fnptr)(
        handle, options, deadline, args, actual_bytes, actual_handles);
}

// This runs in a separate process, since the expected outcome of running this
// function is that the process is shot by the kernel.  It is launched by the
// bad_channel_call_contract_violation test.
static void bad_channel_call(void) {
    char msg[8] = { 0, };

    zx_channel_call_args_t args = {
        .wr_bytes = msg,
        .wr_handles = NULL,
        .wr_num_bytes = sizeof(msg),
        .wr_num_handles = 0,
        .rd_bytes = NULL,
        .rd_handles = NULL,
        .rd_num_bytes = 0,
        .rd_num_handles = 0,
    };

    uint32_t act_bytes = UINT32_MAX;
    uint32_t act_handles = UINT32_MAX;

    zx_handle_t chan = zx_take_startup_handle(PA_HND(PA_USER0, 0));
    zx_handle_t event = zx_take_startup_handle(PA_HND(PA_USER0, 1));

    // Send a copy of the thread handle to the parent, so the parent can suspend
    // this thread.
    zx_handle_t thread;
    zx_status_t status = zx_handle_duplicate(zx_thread_self(), ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status != ZX_OK) {
        zx_object_signal(event, 0, ZX_USER_SIGNAL_0);
        __builtin_trap();
    }
    status = zx_channel_write(chan, 0, NULL, 0, &thread, 1);
    if (status != ZX_OK) {
        zx_object_signal(event, 0, ZX_USER_SIGNAL_0);
        __builtin_trap();
    }

    status = zx_channel_call_noretry(chan, 0, ZX_TIME_INFINITE, &args,
                                     &act_bytes, &act_handles);
    if (status != ZX_ERR_INTERNAL_INTR_RETRY) {
        zx_object_signal(event, 0, ZX_USER_SIGNAL_0);
        __builtin_trap();
    }

    zx_object_signal(event, 0, ZX_USER_SIGNAL_1);

    // Doing another channel call at this point violates the VDSO contract,
    // since we haven't called SYSCALL_zx_channel_call_finish().
    zx_channel_call_noretry(chan, 0, ZX_TIME_INFINITE, &args,
                            &act_bytes, &act_handles);
    zx_object_signal(event, 0, ZX_USER_SIGNAL_0);
    __builtin_trap();
}

// Verify that if an interrupted channel call does not retry and instead a new
// channel call happens, the process dies.
static bool bad_channel_call_contract_violation(void) {
    BEGIN_TEST;

    zx_handle_t chan, remote, event, event_copy;
    ASSERT_EQ(zx_channel_create(0, &chan, &remote), ZX_OK, "");
    ASSERT_EQ(zx_event_create(0, &event), ZX_OK, "");
    ASSERT_EQ(zx_handle_duplicate(event, ZX_RIGHT_SAME_RIGHTS, &event_copy), ZX_OK, "");

    launchpad_t* lp;
    launchpad_create(0, process_bin, &lp);
    launchpad_clone(lp, LP_CLONE_FDIO_STDIO | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);
    const char* args[] = {
        process_bin,
        "child",
    };
    launchpad_set_args(lp, countof(args), args);
    launchpad_add_handle(lp, remote, PA_HND(PA_USER0, 0));
    launchpad_add_handle(lp, event_copy, PA_HND(PA_USER0, 1));
    launchpad_load_from_file(lp, process_bin);
    const char* errmsg;
    zx_handle_t proc;
    ASSERT_EQ(launchpad_go(lp, &proc, &errmsg), ZX_OK, "");

    uint32_t act_bytes = UINT32_MAX;
    uint32_t act_handles = UINT32_MAX;
    zx_handle_t thread;

    // Get the thread handle from our child
    ASSERT_EQ(zx_object_wait_one(chan, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, NULL), ZX_OK, "");
    ASSERT_EQ(zx_channel_read(chan, 0, NULL, &thread, 0, 1, &act_bytes, &act_handles),
              ZX_OK, "");
    ASSERT_EQ(act_handles, 1u, "");

    // Wait for the channel call and pull its message out of the pipe.  This
    // relies on an implementation detail of suspend and channel_call,
    // which is that once the syscall starts, suspend will not be acknowledged
    // until it reaches the wait.  So if we see the message written to the
    // channel, we know the other thread is in the call, and so when we see
    // it has suspended, it will have attempted the wait first.
    EXPECT_EQ(zx_object_wait_one(chan, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, NULL), ZX_OK, "");
    char msg[8] = { 0 };
    ASSERT_EQ(zx_channel_read(chan, 0, msg, NULL, sizeof(msg), 0, &act_bytes, &act_handles),
              ZX_OK, "");

    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend_token(thread, &suspend_token), ZX_OK, "");

    // Wait for the thread to suspend
    zx_signals_t observed = 0u;
    ASSERT_EQ(zx_object_wait_one(thread, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, &observed), ZX_OK, "");

    // Resume the thread
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK, "");

    // Wait for signal 0 or 1, meaning either it's going to try its second call,
    // or something unexpected happened.
    ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1,
                                 ZX_TIME_INFINITE, &observed), ZX_OK, "");
    ASSERT_TRUE(observed & ZX_USER_SIGNAL_1, "");
    ASSERT_FALSE(observed & ZX_USER_SIGNAL_0, "");

    // Process should have been shot
    ASSERT_EQ(zx_object_wait_one(proc, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK, "");
    // Make sure we don't see the "unexpected thing happened" signal.
    ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_0, 0, &observed), ZX_ERR_TIMED_OUT, "");

    ASSERT_EQ(zx_handle_close(event), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(chan), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(thread), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(proc), ZX_OK, "");

    END_TEST;
}

BEGIN_TEST_CASE(channel_fatal_tests)
RUN_TEST(bad_channel_call_contract_violation)
END_TEST_CASE(channel_fatal_tests)

int main(int argc, char** argv) {
    process_bin = argv[0];
    if (argc > 1 && !strcmp(argv[1], "child")) {
        bad_channel_call();
        return 0;
    }
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
