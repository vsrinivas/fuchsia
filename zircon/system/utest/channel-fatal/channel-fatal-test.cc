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
#include <lib/zx/event.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/suspend_token.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zxtest/zxtest.h>

namespace {

const char* process_bin;

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
zx_status_t zx_channel_call_noretry(zx_handle_t handle,
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
void bad_channel_call() {
    char msg[8] = { 0, };

    zx_channel_call_args_t args = {
        .wr_bytes = msg,
        .wr_handles = nullptr,
        .rd_bytes = nullptr,
        .rd_handles = nullptr,
        .wr_num_bytes = sizeof(msg),
        .wr_num_handles = 0,
        .rd_num_bytes = 0,
        .rd_num_handles = 0,
    };

    uint32_t act_bytes = UINT32_MAX;
    uint32_t act_handles = UINT32_MAX;

    zx::channel chan{zx_take_startup_handle(PA_HND(PA_USER0, 0))};
    zx::event event{zx_take_startup_handle(PA_HND(PA_USER0, 1))};

    // Send a copy of the thread handle to the parent, so the parent can suspend
    // this thread.
    zx::thread thread;
    zx_status_t status = zx::thread::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status != ZX_OK) {
        event.signal(0, ZX_USER_SIGNAL_0);
        __builtin_trap();
    }
    zx_handle_t handles[] = {thread.release()};
    status = chan.write(0, nullptr, 0, handles, 1);
    if (status != ZX_OK) {
        event.signal(0, ZX_USER_SIGNAL_0);
        __builtin_trap();
    }

    status = zx_channel_call_noretry(chan.get(), 0, ZX_TIME_INFINITE, &args,
                                     &act_bytes, &act_handles);
    if (status != ZX_ERR_INTERNAL_INTR_RETRY) {
        event.signal(0, ZX_USER_SIGNAL_0);
        __builtin_trap();
    }

    event.signal(0, ZX_USER_SIGNAL_1);

    // Doing another channel call at this point violates the VDSO contract,
    // since we haven't called SYSCALL_zx_channel_call_finish().
    zx_channel_call_noretry(chan.get(), 0, ZX_TIME_INFINITE, &args,
                            &act_bytes, &act_handles);
    event.signal(0, ZX_USER_SIGNAL_0);
    __builtin_trap();
}

// Verify that if an interrupted channel call does not retry and instead a new
// channel call happens, the process dies.
TEST(ChannelFatalTestCase, BadChannelCallContractViolation) {
    zx::channel chan, remote;
    zx::event event, event_copy;
    ASSERT_OK(zx::channel::create(0, &chan, &remote));
    ASSERT_OK(zx::event::create(0, &event));
    ASSERT_OK(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_copy));

    launchpad_t* lp;
    launchpad_create(0, process_bin, &lp);
    launchpad_clone(lp, LP_CLONE_FDIO_STDIO | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);
    const char* args[] = {
        process_bin,
        "child",
    };
    launchpad_set_args(lp, countof(args), args);
    launchpad_add_handle(lp, remote.release(), PA_HND(PA_USER0, 0));
    launchpad_add_handle(lp, event_copy.release(), PA_HND(PA_USER0, 1));
    launchpad_load_from_file(lp, process_bin);
    const char* errmsg;
    zx::process proc;
    ASSERT_OK(launchpad_go(lp, proc.reset_and_get_address(), &errmsg));

    uint32_t act_bytes = UINT32_MAX;
    uint32_t act_handles = UINT32_MAX;
    zx::thread thread;

    // Get the thread handle from our child
    ASSERT_OK(chan.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
    ASSERT_OK(chan.read(0, nullptr, thread.reset_and_get_address(), 0, 1, &act_bytes,
                        &act_handles));
    ASSERT_EQ(act_handles, 1u);

    // Wait for the channel call and pull its message out of the pipe.  This
    // relies on an implementation detail of suspend and channel_call,
    // which is that once the syscall starts, suspend will not be acknowledged
    // until it reaches the wait.  So if we see the message written to the
    // channel, we know the other thread is in the call, and so when we see
    // it has suspended, it will have attempted the wait first.
    EXPECT_OK(chan.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
    char msg[8] = { 0 };
    ASSERT_OK(chan.read(0, msg, nullptr, sizeof(msg), 0, &act_bytes, &act_handles));

    {
        zx::suspend_token suspend_token;
        ASSERT_OK(thread.suspend(&suspend_token));

        // Wait for the thread to suspend
        zx_signals_t observed = 0u;
        ASSERT_OK(thread.wait_one(ZX_THREAD_SUSPENDED, zx::time::infinite(), &observed));

        // Resume the thread
    }

    // Wait for signal 0 or 1, meaning either it's going to try its second call,
    // or something unexpected happened.
    zx_signals_t observed = 0u;
    ASSERT_OK(event.wait_one(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, zx::time::infinite(), &observed));
    ASSERT_TRUE(observed & ZX_USER_SIGNAL_1);
    ASSERT_FALSE(observed & ZX_USER_SIGNAL_0);

    // Process should have been shot
    ASSERT_OK(proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr));
    // Make sure we don't see the "unexpected thing happened" signal.
    ASSERT_EQ(event.wait_one(ZX_USER_SIGNAL_0, zx::time(), &observed), ZX_ERR_TIMED_OUT);
}

} // namespace

int main(int argc, char** argv) {
    process_bin = argv[0];
    if (argc > 1 && !strcmp(argv[1], "child")) {
        bad_channel_call();
        return 0;
    }
    return RUN_ALL_TESTS(argc, argv);
}
