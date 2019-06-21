// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <atomic>

#include <test-utils/test-utils.h>
#include <unittest/unittest.h>
#include <zircon/threads.h>
#include <zircon/status.h>

#include "inferior-control.h"
#include "inferior.h"
#include "utils.h"

namespace {

constexpr uint64_t kExceptionPortKey = 0x123456;

// This is the variable we set the hw watchpoint on.
volatile int gVariableToChange = 0;

std::atomic<bool> gWatchpointThreadShouldContinue;

int watchpoint_function(void* user) {
    while (gWatchpointThreadShouldContinue) {
        gVariableToChange++;
        zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
    }

    return 0;
}

#if defined(__x86_64__)

zx_status_t set_debug_regs(zx_handle_t thread_handle) {
  zx_thread_state_debug_regs_t debug_regs = {};
  // TODO(donosoc): Unify this under one public arch header.
  debug_regs.dr7 = 0b1 |          // L0 = 1 (watchpoint is active).
                   0b01 << 16 |   // R/W0 = 01 (Only data write triggers).
                   0b11 << 18;    // LEN0 = 11 (4 byte watchpoint).

  uint64_t addr = reinterpret_cast<uint64_t>(&gVariableToChange);
  // 4 byte aligned.
  assert((addr & 0b11) == 0);
  debug_regs.dr[0] = reinterpret_cast<uint64_t>(addr);

  return zx_thread_write_state(thread_handle, ZX_THREAD_STATE_DEBUG_REGS,
                               &debug_regs, sizeof(debug_regs));
}

#elif defined(__aarch64__)

zx_status_t set_debug_regs(zx_handle_t thread_handle) {
  zx_thread_state_debug_regs_t debug_regs = {};
  // For now the API is very simple, as zircon is not using further
  // configuration beyond simply adding a write watchpoint.
  // TODO(donosoc): Unify this under one public arch header.
  debug_regs.hw_wps[0].dbgwcr = 0b1;   // DBGWCR_E
  debug_regs.hw_wps[0].dbgwvr = reinterpret_cast<uint64_t>(&gVariableToChange);

  return zx_thread_write_state(thread_handle, ZX_THREAD_STATE_DEBUG_REGS,
                               &debug_regs, sizeof(debug_regs));
}

#else
#error Unsupported arch.
#endif

zx_status_t unset_debug_regs(zx_handle_t thread_handle) {
  zx_thread_state_debug_regs_t debug_regs = {};
  return zx_thread_write_state(thread_handle, ZX_THREAD_STATE_DEBUG_REGS,
                               &debug_regs, sizeof(debug_regs));
}

}  // namespace

bool test_watchpoint_impl(zx_handle_t eport) {
    BEGIN_HELPER;

    gWatchpointThreadShouldContinue = true;

    thrd_t thread;
    thrd_create(&thread, watchpoint_function, nullptr);
    zx_handle_t thread_handle = 0;
    thread_handle = thrd_get_zx_handle(thread);

    zx_status_t status;
    zx_handle_t suspend_token;
    status = zx_task_suspend(thread_handle, &suspend_token);
    ASSERT_EQ(status, ZX_OK);

    zx_signals_t observed;
    status = zx_object_wait_one(thread_handle, ZX_THREAD_SUSPENDED,
                                zx_deadline_after(ZX_TIME_INFINITE), &observed);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_NE((observed & ZX_THREAD_SUSPENDED), 0);

    // Verify that the thread is suspended.
    zx_info_thread thread_info;
    status = zx_object_get_info(thread_handle, ZX_INFO_THREAD, &thread_info,
                                sizeof(thread_info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(thread_info.state, ZX_THREAD_STATE_SUSPENDED);

    unittest_printf("Watchpoint: Writing debug registers.\n");

    status = set_debug_regs(thread_handle);
    ASSERT_EQ(status, ZX_OK);

    unittest_printf("Watchpoint: Resuming thread.\n");

    zx_handle_close(suspend_token);

    // We wait for the exception.
    zx_port_packet_t packet;
    status = zx_port_wait(eport, ZX_TIME_INFINITE, &packet);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet.type));
    ASSERT_EQ(packet.key, kExceptionPortKey);
    ASSERT_EQ(packet.type, ZX_EXCP_HW_BREAKPOINT);

    // Clear the state and resume the thread.
    status = unset_debug_regs(thread_handle);
    ASSERT_EQ(status, ZX_OK);

    gWatchpointThreadShouldContinue = false;

    status = zx_task_resume_from_exception(thread_handle, eport, 0);
    ASSERT_EQ(status, ZX_OK);

    END_HELPER;
}

bool WatchpointTest() {
  BEGIN_TEST;

  // Listen to our own exception port.
  zx_handle_t eport = tu_io_port_create();
  tu_set_exception_port(0, eport, kExceptionPortKey, 0);

  EXPECT_TRUE(test_watchpoint_impl(eport));

  tu_unset_exception_port(0);

  END_TEST;
}

BEGIN_TEST_CASE(watchpoint_start_tests)
RUN_TEST(WatchpointTest)
END_TEST_CASE(watchpoint_start_tests)
