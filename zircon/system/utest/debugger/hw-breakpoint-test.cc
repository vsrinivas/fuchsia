// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/threads.h>

#include <atomic>

#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#include "inferior-control.h"
#include "inferior.h"
#include "utils.h"

namespace {

std::atomic<bool> gBreakpointThreadShouldContinue;

int hw_breakpoint_function(void*) {
  while (gBreakpointThreadShouldContinue) {
    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
  }
  return 0;
}

#if defined(__x86_64__)

zx_status_t set_hw_breakpoint(zx_handle_t thread_handle) {
  // TODO(donosoc): Implement one public place to get the debug masks values.
  zx_thread_state_debug_regs_t debug_regs = {};
  debug_regs.dr[0] = reinterpret_cast<uint64_t>(hw_breakpoint_function);
  debug_regs.dr7 = 1 |          // DR70 Enable
                   0b10 << 18;  // DR70 LEN

  return zx_thread_write_state(thread_handle, ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                               sizeof(debug_regs));
}

#elif defined(__aarch64__)

zx_status_t set_hw_breakpoint(zx_handle_t thread_handle) {
  zx_thread_state_debug_regs_t debug_regs = {};
  auto& hw_bp = debug_regs.hw_bps[0];
  hw_bp.dbgbcr = 1;  // Activate it.
  hw_bp.dbgbvr = reinterpret_cast<uint64_t>(hw_breakpoint_function);

  return zx_thread_write_state(thread_handle, ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                               sizeof(debug_regs));
}

#else
#error Unsupported arch.
#endif

zx_status_t unset_hw_breakpoint(zx_handle_t thread_handle) {
  zx_thread_state_debug_regs_t debug_regs = {};
  return zx_thread_write_state(thread_handle, ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                               sizeof(debug_regs));
}

bool test_hw_breakpoint_impl(zx_handle_t excp_channel) {
  BEGIN_HELPER;

  gBreakpointThreadShouldContinue = false;

  thrd_t thread;
  thrd_create(&thread, hw_breakpoint_function, nullptr);
  zx_handle_t thread_handle = thrd_get_zx_handle(thread);

  zx_status_t status;

  zx_handle_t suspend_token;
  status = zx_task_suspend(thread_handle, &suspend_token);
  ASSERT_EQ(status, ZX_OK);

  zx_signals_t observed;
  status = zx_object_wait_one(thread_handle, ZX_THREAD_SUSPENDED,
                              zx_deadline_after(ZX_TIME_INFINITE), &observed);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_TRUE((observed & ZX_THREAD_SUSPENDED) != 0);

  // Verify that the thread is suspended.
  zx_info_thread thread_info;
  status = zx_object_get_info(thread_handle, ZX_INFO_THREAD, &thread_info, sizeof(thread_info),
                              nullptr, nullptr);
  ASSERT_TRUE(status == ZX_OK);
  ASSERT_TRUE(thread_info.state == ZX_THREAD_STATE_SUSPENDED);

  unittest_printf("HW Breakpoint: Writing debug registers.\n");

  status = set_hw_breakpoint(thread_handle);
  ASSERT_EQ(status, ZX_OK);

  unittest_printf("Watchpoint: Resuming thread.\n");
  zx_handle_close(suspend_token);

  // We wait for the exception.
  tu_channel_wait_readable(excp_channel);

  zx_handle_t exception;
  zx_exception_info_t info;
  uint32_t num_bytes = sizeof(info);
  uint32_t num_handles = 1;
  status =
      zx_channel_read(excp_channel, 0, &info, &exception, num_bytes, num_handles, nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);

  ASSERT_EQ(info.type, ZX_EXCP_HW_BREAKPOINT);

  // Clear the state and resume the thread.
  status = unset_hw_breakpoint(thread_handle);
  ASSERT_EQ(status, ZX_OK);
  gBreakpointThreadShouldContinue = false;

  uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
  status = zx_object_set_property(exception, ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
  ASSERT_EQ(status, ZX_OK);

  zx_handle_close(exception);

  // join the thread.
  int res = -1;
  ASSERT_EQ(thrd_join(thread, &res), thrd_success);
  ASSERT_EQ(res, 0);

  END_HELPER;
}

}  // namespace

bool HWBreakpointTest() {
  BEGIN_TEST;

  // TODO(fxbug.dev/35295): This test flakes.
  END_TEST;

#if defined(__x86_64__)
  // This test crashes QEMU, so for it's disabled for that arch.
  END_TEST;
#endif

  zx_handle_t excp_channel = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_task_create_exception_channel(zx_process_self(), 0, &excp_channel), ZX_OK);

  EXPECT_TRUE(test_hw_breakpoint_impl(excp_channel));

  zx_handle_close(excp_channel);

  END_TEST;
}

BEGIN_TEST_CASE(hw_breakpoint_start_tests)
RUN_TEST(HWBreakpointTest)
END_TEST_CASE(hw_breakpoint_start_tests)
