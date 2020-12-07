// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inferior.h"

#include <assert.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <lib/backtrace-request/backtrace-request.h>
#include <lib/zx/thread.h>
#include <link.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <atomic>

#include <pretty/hexdump.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#include "crash-and-recover.h"
#include "debugger.h"
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

int __NO_INLINE test_segfault_doit2(int* p) { return test_segfault_doit1(p) + *p; }

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
  BEGIN_HELPER;  // Don't stomp on the main thread's current_test_info.

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
        thrd_start_t func = (rqst.type == RQST_START_LOOPING_THREADS ? looping_thread_func
                                                                     : capture_regs_thread_func);
        for (int i = 0; i < kNumExtraThreads; ++i) {
          // For our purposes, we don't need to track the threads.
          // They'll be terminated when the process exits.
          thrd_t thread;
          int ret = thrd_create_with_name(&thread, func, &extra_thread_count, "extra-thread");
          ASSERT_EQ(ret, thrd_success);
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

}  // namespace

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

// Suspend On Start ------------------------------------------------------------

struct suspend_test_state_t {
  std::atomic<bool> running = true;
};

static int suspend_on_start_thread_function(void* user) {
  auto* test_state = reinterpret_cast<suspend_test_state_t*>(user);
  while (test_state->running) {
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  }

  return 0;
}

int test_suspend_on_start() {
  BEGIN_HELPER;

  unittest_printf("Starting second thread.\n");

  suspend_test_state_t test_state = {};
  test_state.running = true;

  thrd_t thread;
  thrd_create(&thread, suspend_on_start_thread_function, &test_state);
  zx_handle_t thread_handle = 0;
  thread_handle = thrd_get_zx_handle(thread);

  unittest_printf("Suspending second thread.\n");

  zx_status_t status;
  zx_handle_t suspend_token;
  status = zx_task_suspend(thread_handle, &suspend_token);
  if (status != ZX_OK) {
    unittest_printf("Watchpoint: Could not suspend thread: %s\n", zx_status_get_string(status));
    exit(20);
  }

  // Verify that the thread is suspended.

  zx_signals_t observed;
  status = zx_object_wait_one(thread_handle, ZX_THREAD_SUSPENDED,
                              zx_deadline_after(ZX_TIME_INFINITE), &observed);
  if (status != ZX_OK) {
    unittest_printf("Watchpoint: Could not get suspended signal: %s\n",
                    zx_status_get_string(status));
    exit(20);
  }
  ASSERT_TRUE((observed & ZX_THREAD_SUSPENDED) != 0);

  // Verify that the thread is suspended.
  zx_info_thread thread_info;
  status = zx_object_get_info(thread_handle, ZX_INFO_THREAD, &thread_info, sizeof(thread_info),
                              nullptr, nullptr);
  ASSERT_TRUE(status == ZX_OK);
  ASSERT_TRUE(thread_info.state == ZX_THREAD_STATE_SUSPENDED);

  unittest_printf("Obtaining general regs.\n");

  // We should be able to read regs.
  zx_thread_state_general_regs_t gregs;
  status = zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS, &gregs, sizeof(gregs));
  if (status != ZX_OK) {
    unittest_printf("Could not obtain general registers: %s\n", zx_status_get_string(status));
    exit(20);
  }

  unittest_printf("Successfully got registers. Test successful.\n");

  // Resume the second thread.
  test_state.running = false;
  zx_handle_close(suspend_token);

  int res = 1;
  thrd_join(thread, &res);
  ASSERT_TRUE(res == 0);

  return kInferiorReturnCode;

  END_HELPER;
}

int test_dyn_break_on_load() {
  BEGIN_HELPER;

  zx_handle_t self_handle = zx_process_self();

  // Load a .so several times. These should trigger an exception.
  for (int i = 0; i < 5; i++) {
    void* h = dlopen("libdlopen-indirect-deps-test-module.so", RTLD_LOCAL);
    ASSERT_NONNULL(h, dlerror());
    EXPECT_EQ(dlclose(h), 0, "dlclose failed");
  }

  // Disable the property so that there are not exceptions triggered.
  uintptr_t break_on_load = 0;
  zx_status_t status = zx_object_set_property(self_handle, ZX_PROP_PROCESS_BREAK_ON_LOAD,
                                              &break_on_load, sizeof(break_on_load));
  ASSERT_EQ(status, ZX_OK);

  // Load a .so several times. These should not trigger an exception.
  for (int i = 0; i < 5; i++) {
    void* h = dlopen("libdlopen-indirect-deps-test-module.so", RTLD_LOCAL);
    ASSERT_NONNULL(h, dlerror());
    EXPECT_EQ(dlclose(h), 0, "dlclose failed");
  }

  // Re-Enable the property so that there are not exceptions triggered.
  break_on_load = 1;
  status = zx_object_set_property(self_handle, ZX_PROP_PROCESS_BREAK_ON_LOAD, &break_on_load,
                                  sizeof(break_on_load));
  ASSERT_EQ(status, ZX_OK);

  // Load a .so several times. These should trigger an exception.
  for (int i = 0; i < 4; i++) {
    void* h = dlopen("libdlopen-indirect-deps-test-module.so", RTLD_LOCAL);
    ASSERT_NONNULL(h, dlerror());
    EXPECT_EQ(dlclose(h), 0, "dlclose failed");
  }

  return kInferiorReturnCode;

  END_HELPER;
}
