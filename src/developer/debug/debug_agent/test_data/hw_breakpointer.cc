// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hw_breakpointer_helpers.h"

// This is a self contained binary that is meant to be run *manually*. This is the smallest code
// that can be used to reproduce a HW breakpoint exception.
// This is meant to be able to test the functionality of zircon without having to go throught the
// hassle of having the whole debugger context around.
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

namespace {

// Test Cases ======================================================================================

// BreakOnFunction ---------------------------------------------------------------------------------

// This is the function that we will set up the breakpoint to.
static int __NO_INLINE FunctionToBreakpointOn(int c) { return c + c; }

// This is the code that the new thread will run.
// It's meant to be an eternal loop.
int BreakOnFunctionThreadFunction(void* user) {
  ThreadSetup* thread_setup = reinterpret_cast<ThreadSetup*>(user);

  // We signal the test harness that we are here.
  thread_setup->event.signal(kHarnessToThread, kThreadToHarness);

  PRINT("Signaled harness.");

  // We wait now for the harness to tell us we can continue.
  CHECK_OK(thread_setup->event.wait_one(kHarnessToThread, zx::time::infinite(), nullptr));

  PRINT("Got signaled by harness.");

  int counter = 1;
  while (thread_setup->test_running) {
    // We use write to avoid deadlocking with the outside libc calls.
    write(1, kBeacon, sizeof(kBeacon));
    counter = FunctionToBreakpointOn(counter);
    zx::nanosleep(zx::deadline_after(zx::sec(1)));
  }

  return 0;
}

int BreakOnFunctionTestCase() {
  PRINT("Running HW breakpoint when calling a function test.");

  auto thread_setup = CreateTestSetup(BreakOnFunctionThreadFunction);

  auto [port, exception_channel] = WaitAsyncOnExceptionChannel(thread_setup->thread);

  uint64_t breakpoint_address = reinterpret_cast<uint64_t>(FunctionToBreakpointOn);
  InstallHWBreakpoint(thread_setup->thread, breakpoint_address);
  PRINT("Set breakpoint on function \"FunctionToBreakpointOn\" 0x%zx", breakpoint_address);

  // Tell the thread to continue.
  thread_setup->event.signal(kThreadToHarness, kHarnessToThread);

  // We wait until we receive an exception.
  Exception exception = WaitForException(port, exception_channel);

  FXL_DCHECK(exception.info.type == ZX_EXCP_HW_BREAKPOINT);
  PRINT("Got HW exception!");

  // Remove the hw breakpoint.
  thread_setup->test_running = false;
  InstallHWBreakpoint(thread_setup->thread, 0);
  ResumeException(thread_setup->thread, std::move(exception));

  return 0;
}

}  // namespace

// Main --------------------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    return BreakOnFunctionTestCase();
}
