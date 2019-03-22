// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/threads.h>

#include "lib/fxl/logging.h"

#include <unistd.h>

// This is a self contained binary that is meant to be run *manually*.
// This is the smallest code that can be used to reproduce a HW breakpoint
// exception. This is meant to be able to test the functionality of zircon
// without having to go throught the hassle of having the whole debugger
// context around.
//
// THIS CODE IS MEANT TO CRASH WITH A HW EXCEPTION WHEN WORKING PROPERLY!
//
// The basic setup is:
//
// 1. Create a thread that will loop forever, continually calling a particular
//    function.
// 2. Suspend that thread.
// 3. Install a HW breakpoint through zx_thread_write_state.
// 4. Resume the thread.
// 5. Wait for some time for the exception. If the exception never happened, it
//    means that Zircon is not doing the right thing.

constexpr char kBeacon[] = "Counter: Thread running.\n";

// This is the function that we will set up the breakpoint to.
int __NO_INLINE FunctionToBreakpointOn(int c) { return c + c; }

// This is the code that the new thread will run.
// It's meant to be an eternal loop.
int ThreadFunction(void*) {
  int counter = 1;
  while (true) {
    // We use write to avoid deadlocking with the outside libc calls.
    write(1, kBeacon, sizeof(kBeacon));
    counter = FunctionToBreakpointOn(counter);
    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
  }

  return 0;
}

int main() {
  FXL_LOG(INFO) << "****** Creating thread.";

  thrd_t thread;
  thrd_create(&thread, ThreadFunction, nullptr);
  zx_handle_t thread_handle = 0;
  thread_handle = thrd_get_zx_handle(thread);

  FXL_LOG(INFO) << "****** Suspending thread.";

  zx_status_t status;
  zx_handle_t suspend_token;
  status = zx_task_suspend(thread_handle, &suspend_token);
  FXL_DCHECK(status == ZX_OK) << "Could not suspend thread: " << status;

  zx_signals_t observed;
  status = zx_object_wait_one(thread_handle, ZX_THREAD_SUSPENDED,
                              zx_deadline_after(ZX_MSEC(500)), &observed);
  FXL_DCHECK(status == ZX_OK) << "Could not get suspended signal: " << status;

  FXL_LOG(INFO) << "****** Writing debug registers.";

  zx_thread_state_debug_regs_t debug_regs = {};
  auto& hw_bp = debug_regs.hw_bps[0];
  hw_bp.dbgbcr = 1;  // Activate it.
  hw_bp.dbgbvr = reinterpret_cast<uint64_t>(FunctionToBreakpointOn);

  FXL_LOG(INFO) << "DBGBVR: 0x" << std::hex << debug_regs.hw_bps[0].dbgbvr;

  status = zx_thread_write_state(thread_handle, ZX_THREAD_STATE_DEBUG_REGS,
                                 &debug_regs, sizeof(debug_regs));
  FXL_DCHECK(status == ZX_OK) << "Could not write debug regs: " << status;

  FXL_LOG(INFO) << "****** Resuming thread.";

  status = zx_handle_close(suspend_token);
  FXL_DCHECK(status == ZX_OK) << "Could not resume thread: " << status;

  FXL_LOG(INFO) << "****** Waiting for a bit to hit the breakpoint.";

  // The other thread won't ever stop, so there is no use waiting for a
  // terminated signal. Instead we wait for a generous amount of time for the
  // HW exception to happen.
  // If it doesn't happen, it's an error.
  zx_nanosleep(zx_deadline_after(ZX_SEC(10)));

  FXL_LOG(ERROR) << " THIS IS AN ERROR. THIS BINARY SHOULD'VE CRASHED!";
  return 1;
}
