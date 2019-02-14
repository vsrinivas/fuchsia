// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <atomic>
#include <inttypes.h>
#include <link.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <lib/backtrace-request/backtrace-request.h>
#include <lib/zx/thread.h>
#include <pretty/hexdump.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include "crash-and-recover.h"
#include "debugger.h"
#include "inferior.h"
#include "inferior-control.h"
#include "utils.h"

namespace {

// Produce a backtrace of sufficient size to be interesting but not excessive.
constexpr int kTestSegfaultDepth = 4;

// Compilers are getting too smart.
// These maintain the semantics we want even under optimization.

volatile int* crashing_ptr = (int*)42;
volatile int crash_depth;

// This is used to cause fp != sp when the crash happens on arm64.
int leaf_stack_size = 10;

std::atomic<int> extra_thread_count;

int __NO_INLINE test_segfault_doit2(int*);

int __NO_INLINE test_segfault_leaf(int n, int* p) {
    volatile int x[n];
    x[0] = *p;
    *crashing_ptr = x[0];
    return 0;
}

int __NO_INLINE test_segfault_doit1(int* p) {
    if (crash_depth > 0) {
        int n = crash_depth;
        int use_stack[n];
        memset(use_stack, 0x99, n * sizeof(int));
        --crash_depth;
        return test_segfault_doit2(use_stack) + 99;
    }
    return test_segfault_leaf(leaf_stack_size, p) + 99;
}

int __NO_INLINE test_segfault_doit2(int* p) {
    return test_segfault_doit1(p) + *p;
}

int looping_thread_func(void* arg) {
    auto thread_count_ptr = reinterpret_cast<std::atomic<int>*>(arg);
    atomic_fetch_add(thread_count_ptr, 1);
    unittest_printf("Extra thread started.\n");
    while (true)
        zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
    return 0;
}

// This returns a bool as it's a unittest "helper" routine.

bool msg_loop(zx_handle_t channel) {
    BEGIN_HELPER; // Don't stomp on the main thread's current_test_info.

    bool my_done_tests = false;

    while (!my_done_tests) {
        request_message_t rqst;
        ASSERT_TRUE(recv_request(channel, &rqst), "");
        switch (rqst.type) {
        case RQST_DONE:
            my_done_tests = true;
            break;
        case RQST_PING:
            send_simple_response(channel, RESP_PONG);
            break;
        case RQST_CRASH_AND_RECOVER_TEST:
            for (int i = 0; i < kNumSegvTries; ++i) {
                if (!test_prep_and_segv())
                    exit(21);
            }
            send_simple_response(channel, RESP_RECOVERED_FROM_CRASH);
            break;
        case RQST_START_LOOPING_THREADS:
        case RQST_START_CAPTURE_REGS_THREADS: {
            extra_thread_count.store(0);
            thrd_start_t func = (rqst.type == RQST_START_LOOPING_THREADS
                                 ? looping_thread_func
                                 : capture_regs_thread_func);
            for (int i = 0; i < kNumExtraThreads; ++i) {
                // For our purposes, we don't need to track the threads.
                // They'll be terminated when the process exits.
                thrd_t thread;
                tu_thread_create_c11(&thread, func, &extra_thread_count, "extra-thread");
            }
            // Wait for all threads to be started.
            // Each will require an ZX_EXCP_THREAD_STARTING exchange with the "debugger".
            while (extra_thread_count.load() < kNumExtraThreads)
                zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
            send_simple_response(channel, RESP_THREADS_STARTED);
            break;
        }
        case RQST_GET_THREAD_HANDLE: {
            zx_handle_t self = zx_thread_self();
            zx_handle_t copy;
            zx_handle_duplicate(self, ZX_RIGHT_SAME_RIGHTS, &copy);
            // Note: The handle is transferred to the receiver.
            response_message_t resp{};
            resp.type = RESP_THREAD_HANDLE;
            unittest_printf("sending handle %d response on channel %u\n", copy, channel);
            send_response_with_handle(channel, resp, copy);
            break;
        }
        case RQST_GET_LOAD_ADDRS: {
            response_message_t resp{};
            resp.type = RESP_LOAD_ADDRS;
            resp.payload.load_addrs.libc_load_addr = get_libc_load_addr();
            resp.payload.load_addrs.exec_load_addr = get_exec_load_addr();
            send_response(channel, resp);
            break;
        }
        default:
            unittest_printf("unknown request received: %d\n", rqst.type);
            break;
        }
    }

    END_HELPER;
}

} // namespace

// Produce a crash with a moderately interesting backtrace.
int __NO_INLINE test_segfault() {
    crash_depth = kTestSegfaultDepth;
    int i = 0;
    return test_segfault_doit1(&i);
}

// Invoke the s/w breakpoint insn using the crashlogger mechanism
// to request a backtrace but not terminate the process.
int __NO_INLINE test_sw_break() {
    unittest_printf("Invoking s/w breakpoint instruction\n");
    backtrace_request();
    unittest_printf("Resumed after s/w breakpoint instruction\n");
    return 0;
}

int test_inferior() {
    zx_handle_t channel = zx_take_startup_handle(PA_USER0);
    unittest_printf("test_inferior: got handle %d\n", channel);

    if (!msg_loop(channel))
        exit(20);

    unittest_printf("Inferior done\n");

    // This value is explicitly tested for.
    return kInferiorReturnCode;
}
