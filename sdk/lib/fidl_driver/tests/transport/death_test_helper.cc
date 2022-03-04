// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/fidl_driver/tests/transport/death_test_helper.h"

#include <lib/zx/thread.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

namespace fidl_driver_testing {

void CurrentThreadExceptionHandler::Try(fit::closure statement) {
  ASSERT_OK(zx::thread::self()->create_exception_channel(0, &exception_channel_));
  monitoring_exception_.Signal();
  statement();
}

void CurrentThreadExceptionHandler::WaitForOneSwBreakpoint() {
  zx_signals_t out_signals;
  ASSERT_OK(monitoring_exception_.Wait());
  ASSERT_OK(exception_channel_.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &out_signals));
  ASSERT_OK(exception_channel_.read(0, &exception_.info, exception_.handle.reset_and_get_address(),
                                    sizeof(exception_.info), 1, nullptr, nullptr));
  ASSERT_TRUE(exception_.handle.is_valid());
  ASSERT_EQ(ZX_EXCP_SW_BREAKPOINT, exception_.info.type);

  // The following logic is similar to `cleanup_backtrace_request`.
  // See zircon/system/ulib/backtrace-request/backtrace-request-utils.cc

#if defined(__aarch64__)
  // On ARM64, we need to advance the thread past the SW breakpoint instruction.
  // All instructions are 4 bytes.
  zx::thread thread;
  zx_status_t status = exception_.handle.get_thread(&thread);
  if (status != ZX_OK) {
    FAIL("Failed to obtain thread from exception handle");
  }
  zx_thread_state_general_regs_t regs;
  status = thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  if (status != ZX_OK) {
    FAIL("Failed to read exception thread state");
  }
  regs.pc += 4;
  status = thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  if (status != ZX_OK) {
    FAIL("Failed to write exception thread state");
  }
#elif defined(__x86_64__)
  // On x86, the pc is left at one past the s/w break insn,
  // so there's nothing more we need to do.
#else
#error "what machine?"
#endif

  // Resume the thread raising exception.
  uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
  ASSERT_OK(exception_.handle.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state)));
  exception_ = {};
  exception_channel_.reset();
  monitoring_exception_.Reset();
}

}  // namespace fidl_driver_testing
