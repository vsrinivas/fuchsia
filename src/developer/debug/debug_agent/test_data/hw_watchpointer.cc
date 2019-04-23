// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/threads.h>

#include "src/lib/fxl/logging.h"

#include <unistd.h>

// This is a self contained binary that is meant to be run *manually*.
// This is the smallest code that can be used to reproduce a HW watchpoint
// exception. This is meant to be able to test the functionality of zircon
// without having to go throught the hassle of having the whole debugger context
// around.
//
// THIS CODE IS MEANT TO CRASH WITH A HW EXCEPTION WHEN WORKING PROPERLY!
//
// The basic setup is:
//
// 1. Create a thread that will loop forever, continually calling a particular
//    function.
// 2. Suspend that thread.
// 3. Install a HW watchpoint through zx_thread_write_state.
// 4. Resume the thread.
// 5. Wait for some time for the exception. If the exception never happened, it
//    means that Zircon is not doing the right thing.

// This is the variable we set the hw watchpoint on.
volatile int kVariableToChange = 0;

static constexpr char kBeacon[] = "Counter: Thread running.\n";

// This is the code that the new thread will run.
// It's meant to be an eternal loop.
int ThreadFunction(void*) {
  while (true) {
    // We use write to avoid deadlocking with the outside libc calls.
    write(1, kBeacon, sizeof(kBeacon));
    kVariableToChange++;
    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
  }

  return 0;
}

#if defined(__x86_64__)

zx_thread_state_debug_regs_t GetDebugRegs() {
  zx_thread_state_debug_regs_t debug_regs = {};
  debug_regs.dr7 = 0b1 |          // L0 = 1 (watchpoint is active).
                   0b01 << 16 |   // R/W0 = 01 (Only data write triggers).
                   0b11 << 18;    // LEN0 = 11 (4 byte watchpoint).

  // 4 byte aligned.
  debug_regs.dr[0] = reinterpret_cast<uint64_t>(&kVariableToChange) & ~0b11;
  return debug_regs;
}

#elif defined(__aarch64__)

zx_thread_state_debug_regs_t GetDebugRegs() {
  FXL_NOTREACHED() << "Arm64 side not implemented.";
  return {};
}

#else
#error Unsupported arch.
#endif

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

  FXL_LOG(INFO) << "****** Writing watchpoint.";

  auto debug_regs = GetDebugRegs();
  status = zx_thread_write_state(thread_handle, ZX_THREAD_STATE_DEBUG_REGS,
                                 &debug_regs, sizeof(debug_regs));
  FXL_DCHECK(status == ZX_OK) << "Could not write debug regs: " << status;

  FXL_LOG(INFO) << "****** Resuming thread.";

  status = zx_handle_close(suspend_token);
  FXL_DCHECK(status == ZX_OK) << "Could not resume thread: " << status;

  FXL_LOG(INFO) << "****** Waiting for a bit to hit the watchpoint.";

  // The other thread won't ever stop, so there is no use waiting for a
  // terminated signal. Instead we wait for a generous amount of time for the
  // HW exception to happen.
  // If it doesn't happen, it's an error.
  zx_nanosleep(zx_deadline_after(ZX_SEC(10)));

  FXL_LOG(ERROR) << " THIS IS AN ERROR. THIS BINARY SHOULD'VE CRASHED!";
  return 1;


}
