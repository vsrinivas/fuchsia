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

// This returns a bool as it's a unittest "helper" routine.
// N.B. This runs on the wait-inferior thread.

bool handle_expected_page_fault(zx_handle_t inferior,
                                zx_handle_t port,
                                const zx_port_packet_t* packet,
                                std::atomic<int>* segv_count) {
    BEGIN_HELPER;

    unittest_printf("wait-inf: got page fault exception\n");

    zx_koid_t tid = packet->exception.tid;
    zx_handle_t thread = tu_get_thread(inferior, tid);

    dump_inferior_regs(thread);

    // Verify that the fault is at the PC we expected.
    if (!test_segv_pc(thread))
        return false;

    // Do some tests that require a suspended inferior.
    test_memory_ops(inferior, thread);

    fix_inferior_segv(thread);
    // Useful for debugging, otherwise a bit too verbose.
    // dump_inferior_regs(thread);

    // Increment this before resuming the inferior in case the inferior
    // sends RESP_RECOVERED_FROM_CRASH and the testcase processes the message
    // before we can increment it.
    atomic_fetch_add(segv_count, 1);

    zx_status_t status = zx_task_resume_from_exception(thread, port, 0);
    tu_handle_close(thread);
    ASSERT_EQ(status, ZX_OK);

    END_HELPER;
}

// N.B. This runs on the wait-inferior thread.

bool debugger_test_exception_handler(zx_handle_t inferior, zx_handle_t port,
                                     const zx_port_packet_t* packet,
                                     void* handler_arg) {
    BEGIN_HELPER;

    // Note: This may be NULL if the test is not expecting a page fault.
    std::atomic<int>* segv_count = static_cast<std::atomic<int>*>(handler_arg);

    zx_koid_t pid = tu_get_koid(inferior);

    if (ZX_PKT_IS_SIGNAL_ONE(packet->type)) {
        ASSERT_TRUE(packet->key != pid);
        // Must be a signal on one of the threads.
        // Here we're only expecting TERMINATED.
        ASSERT_TRUE(packet->signal.observed & ZX_THREAD_TERMINATED);
    } else {
        ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet->type));

        zx_koid_t tid = packet->exception.tid;

        switch (packet->type) {
        case ZX_EXCP_THREAD_STARTING:
            unittest_printf("wait-inf: inferior started\n");
            if (!resume_inferior(inferior, port, tid))
                return false;
            break;

        case ZX_EXCP_THREAD_EXITING:
            // N.B. We could get thread exiting messages from previous
            // tests.
            EXPECT_TRUE(handle_thread_exiting(inferior, port, packet));
            break;

        case ZX_EXCP_FATAL_PAGE_FAULT:
            ASSERT_NONNULL(segv_count);
            ASSERT_TRUE(handle_expected_page_fault(inferior, port, packet, segv_count));
            break;

        default: {
            char msg[128];
            snprintf(msg, sizeof(msg), "unexpected packet type: 0x%x", packet->type);
            ASSERT_TRUE(false, msg);
            __UNREACHABLE;
        }
        }
    }

    END_HELPER;
}

bool DebuggerTest() {
    BEGIN_TEST;

    launchpad_t* lp;
    zx_handle_t inferior, channel;
    if (!setup_inferior(kTestInferiorChildName, &lp, &inferior, &channel))
        return false;

    std::atomic<int> segv_count;

    expect_debugger_attached_eq(inferior, false, "debugger should not appear attached");
    zx_handle_t eport = tu_io_port_create();
    size_t max_threads = 10;
    inferior_data_t* inferior_data = attach_inferior(inferior, eport, max_threads);
    thrd_t wait_inf_thread =
        start_wait_inf_thread(inferior_data, debugger_test_exception_handler, &segv_count);
    EXPECT_NE(eport, ZX_HANDLE_INVALID);
    expect_debugger_attached_eq(inferior, true, "debugger should appear attached");

    if (!start_inferior(lp))
        return false;
    if (!verify_inferior_running(channel))
        return false;

    segv_count.store(0);
    send_simple_request(channel, RQST_CRASH_AND_RECOVER_TEST);
    EXPECT_TRUE(recv_simple_response(channel, RESP_RECOVERED_FROM_CRASH), "");
    EXPECT_EQ(segv_count.load(), kNumSegvTries, "segv tests terminated prematurely");

    if (!shutdown_inferior(channel, inferior))
        return false;

    // Stop the waiter thread before closing the eport that it's waiting on.
    join_wait_inf_thread(wait_inf_thread);

    detach_inferior(inferior_data, false);

    expect_debugger_attached_eq(inferior, true, "debugger should still appear attached");
    tu_handle_close(eport);
    expect_debugger_attached_eq(inferior, false, "debugger should no longer appear attached");

    tu_handle_close(channel);
    tu_handle_close(inferior);

    END_TEST;
}

bool DebuggerThreadListTest() {
    BEGIN_TEST;

    launchpad_t* lp;
    zx_handle_t inferior, channel;
    if (!setup_inferior(kTestInferiorChildName, &lp, &inferior, &channel))
        return false;

    zx_handle_t eport = tu_io_port_create();
    size_t max_threads = 10;
    inferior_data_t* inferior_data = attach_inferior(inferior, eport, max_threads);
    thrd_t wait_inf_thread =
        start_wait_inf_thread(inferior_data, debugger_test_exception_handler, NULL);
    EXPECT_NE(eport, ZX_HANDLE_INVALID);

    if (!start_inferior(lp))
        return false;
    if (!verify_inferior_running(channel))
        return false;

    send_simple_request(channel, RQST_START_LOOPING_THREADS);
    EXPECT_TRUE(recv_simple_response(channel, RESP_THREADS_STARTED), "");

    // This doesn't use tu_process_get_threads() because here we're testing
    // various aspects of ZX_INFO_PROCESS_THREADS.
    uint32_t buf_size = 100 * sizeof(zx_koid_t);
    size_t num_threads;
    zx_koid_t* threads = static_cast<zx_koid_t*>(tu_malloc(buf_size));
    zx_status_t status = zx_object_get_info(inferior, ZX_INFO_PROCESS_THREADS, threads, buf_size,
                                            &num_threads, NULL);
    ASSERT_EQ(status, ZX_OK);

    // There should be at least 1+kNumExtraThreads threads in the result.
    ASSERT_GE(num_threads, 1 + kNumExtraThreads, "zx_object_get_info failed");

    // Verify each entry is valid.
    for (uint32_t i = 0; i < num_threads; ++i) {
        zx_koid_t koid = threads[i];
        unittest_printf("Looking up thread %llu\n", (long long)koid);
        zx_handle_t thread = tu_get_thread(inferior, koid);
        zx_info_handle_basic_t info;
        status = zx_object_get_info(thread, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
        EXPECT_EQ(status, ZX_OK, "zx_object_get_info failed");
        EXPECT_EQ(info.type, ZX_OBJ_TYPE_THREAD, "not a thread");
    }

    if (!shutdown_inferior(channel, inferior))
        return false;

    // Stop the waiter thread before closing the eport that it's waiting on.
    join_wait_inf_thread(wait_inf_thread);

    detach_inferior(inferior_data, true);

    tu_handle_close(eport);
    tu_handle_close(channel);
    tu_handle_close(inferior);

    END_TEST;
}

bool PropertyProcessDebugAddrTest() {
    BEGIN_TEST;

    zx_handle_t self = zx_process_self();

    // We shouldn't be able to set it.
    uintptr_t debug_addr = 42;
    zx_status_t status =
        zx_object_set_property(self, ZX_PROP_PROCESS_DEBUG_ADDR, &debug_addr, sizeof(debug_addr));
    ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED);

    // Some minimal verification that the value is correct.

    status =
        zx_object_get_property(self, ZX_PROP_PROCESS_DEBUG_ADDR, &debug_addr, sizeof(debug_addr));
    ASSERT_EQ(status, ZX_OK);

    // These are all dsos we link with. See rules.mk.
    const char* launchpad_so = "liblaunchpad.so";
    bool found_launchpad = false;
    const char* libc_so = "libc.so";
    bool found_libc = false;
    const char* test_utils_so = "libtest-utils.so";
    bool found_test_utils = false;
    const char* unittest_so = "libunittest.so";
    bool found_unittest = false;

    const r_debug* debug = (r_debug*)debug_addr;
    const link_map* lmap = debug->r_map;

    EXPECT_EQ(debug->r_state, r_debug::RT_CONSISTENT);

    while (lmap != NULL) {
        if (strcmp(lmap->l_name, launchpad_so) == 0)
            found_launchpad = true;
        else if (strcmp(lmap->l_name, libc_so) == 0)
            found_libc = true;
        else if (strcmp(lmap->l_name, test_utils_so) == 0)
            found_test_utils = true;
        else if (strcmp(lmap->l_name, unittest_so) == 0)
            found_unittest = true;
        lmap = lmap->l_next;
    }

    EXPECT_TRUE(found_launchpad);
    EXPECT_TRUE(found_libc);
    EXPECT_TRUE(found_test_utils);
    EXPECT_TRUE(found_unittest);

    END_TEST;
}

int write_text_segment_helper() __ALIGNED(8);
int write_text_segment_helper() {
    /* This function needs to be at least two bytes in size as we set a
       breakpoint, figuratively speaking, on write_text_segment_helper + 1
       to ensure the address is not page aligned. Returning some random value
       will ensure that. */
    return 42;
}

bool WriteTextSegmentTest() {
    BEGIN_TEST;

    zx_handle_t self = zx_process_self();

    // Exercise ZX-739
    // Pretend we're writing a s/w breakpoint to the start of this function.

    // write_text_segment_helper is suitably aligned, add 1 to ensure the
    // byte we write is not page aligned.
    uintptr_t addr = (uintptr_t)write_text_segment_helper + 1;
    uint8_t previous_byte;
    size_t size = read_inferior_memory(self, addr, &previous_byte, sizeof(previous_byte));
    EXPECT_EQ(size, sizeof(previous_byte));

    uint8_t byte_to_write = 0;
    size = write_inferior_memory(self, addr, &byte_to_write, sizeof(byte_to_write));
    EXPECT_EQ(size, sizeof(byte_to_write));

    size = write_inferior_memory(self, addr, &previous_byte, sizeof(previous_byte));
    EXPECT_EQ(size, sizeof(previous_byte));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(debugger_tests)
RUN_TEST(DebuggerTest)
RUN_TEST(DebuggerThreadListTest)
RUN_TEST(PropertyProcessDebugAddrTest)
RUN_TEST(WriteTextSegmentTest)
END_TEST_CASE(debugger_tests)
