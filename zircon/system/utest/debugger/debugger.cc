// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugger.h"

#include <assert.h>
#include <inttypes.h>
#include <lib/backtrace-request/backtrace-request.h>
#include <lib/zx/exception.h>
#include <lib/zx/thread.h>
#include <link.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
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
#include "inferior-control.h"
#include "inferior.h"
#include "utils.h"

namespace {

// This returns a bool as it's a unittest "helper" routine.
// N.B. This runs on the wait-inferior thread.

bool handle_expected_page_fault(zx_handle_t inferior, const zx_exception_info_t* info,
                                zx::exception exception, std::atomic<int>* segv_count) {
  BEGIN_HELPER;

  unittest_printf("wait-inf: got page fault exception\n");

  zx::thread thread;
  ASSERT_EQ(exception.get_thread(&thread), ZX_OK);

  dump_inferior_regs(thread.get());

  // Verify that the fault is at the PC we expected.
  if (!test_segv_pc(thread.get()))
    return false;

  // Do some tests that require a suspended inferior.
  test_memory_ops(inferior, thread.get());

  fix_inferior_segv(thread.get());
  // Useful for debugging, otherwise a bit too verbose.
  // dump_inferior_regs(thread);

  // Increment this before resuming the inferior in case the inferior
  // sends RESP_RECOVERED_FROM_CRASH and the testcase processes the message
  // before we can increment it.
  atomic_fetch_add(segv_count, 1);

  thread.reset();

  uint32_t exception_state = ZX_EXCEPTION_STATE_HANDLED;
  EXPECT_EQ(
      exception.set_property(ZX_PROP_EXCEPTION_STATE, &exception_state, sizeof(exception_state)),
      ZX_OK);

  END_HELPER;
}

// N.B. This runs on the wait-inferior thread.

bool debugger_test_exception_handler(inferior_data_t* data, const zx_port_packet_t* packet,
                                     void* handler_arg) {
  BEGIN_HELPER;

  // Note: This may be NULL if the test is not expecting a page fault.
  std::atomic<int>* segv_count = static_cast<std::atomic<int>*>(handler_arg);

  zx_info_handle_basic_t basic_info;
  zx_status_t status = zx_object_get_info(data->inferior, ZX_INFO_HANDLE_BASIC, &basic_info,
                                          sizeof(basic_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t pid = basic_info.koid;

  ASSERT_TRUE(ZX_PKT_IS_SIGNAL_ONE(packet->type));

  status = zx_object_get_info(data->exception_channel, ZX_INFO_HANDLE_BASIC, &basic_info,
                              sizeof(basic_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);

  if (packet->key != basic_info.koid) {
    ASSERT_TRUE(packet->key != pid);
    // Must be a signal on one of the threads.
    // Here we're only expecting TERMINATED.
    ASSERT_TRUE(packet->signal.observed & ZX_THREAD_TERMINATED);
  } else {
    zx::exception exception;
    zx_exception_info_t info;
    uint32_t num_bytes = sizeof(info);
    uint32_t num_handles = 1;
    zx_status_t status =
        zx_channel_read(data->exception_channel, 0, &info, exception.reset_and_get_address(),
                        num_bytes, num_handles, nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);

    switch (info.type) {
      case ZX_EXCP_THREAD_STARTING:
        unittest_printf("wait-inf: inferior started\n");
        break;

      case ZX_EXCP_THREAD_EXITING:
        // N.B. We could get thread exiting messages from previous
        // tests.
        EXPECT_TRUE(handle_thread_exiting(data->inferior, &info, std::move(exception)));
        break;

      case ZX_EXCP_FATAL_PAGE_FAULT:
        ASSERT_NONNULL(segv_count);
        ASSERT_TRUE(
            handle_expected_page_fault(data->inferior, &info, std::move(exception), segv_count));
        break;

      default: {
        char msg[128];
        snprintf(msg, sizeof(msg), "unexpected exception type: 0x%x", info.type);
        ASSERT_TRUE(false, msg);
        __UNREACHABLE;
      }
    }
  }

  END_HELPER;
}

bool DebuggerTest() {
  BEGIN_TEST;

  springboard_t* sb;
  zx_handle_t inferior, channel;
  if (!setup_inferior(kTestInferiorChildName, &sb, &inferior, &channel))
    return false;

  std::atomic<int> segv_count;

  expect_debugger_attached_eq(inferior, false, "debugger should not appear attached");
  zx_handle_t port = ZX_HANDLE_INVALID;
  EXPECT_EQ(zx_port_create(0, &port), ZX_OK);
  size_t max_threads = 10;
  inferior_data_t* inferior_data = attach_inferior(inferior, port, max_threads);
  thrd_t wait_inf_thread =
      start_wait_inf_thread(inferior_data, debugger_test_exception_handler, &segv_count);
  EXPECT_NE(port, ZX_HANDLE_INVALID);
  expect_debugger_attached_eq(inferior, true, "debugger should appear attached");

  if (!start_inferior(sb))
    return false;
  if (!verify_inferior_running(channel))
    return false;

  segv_count.store(0);
  send_simple_request(channel, RQST_CRASH_AND_RECOVER_TEST);
  EXPECT_TRUE(recv_simple_response(channel, RESP_RECOVERED_FROM_CRASH), "");
  EXPECT_EQ(segv_count.load(), kNumSegvTries, "segv tests terminated prematurely");

  expect_debugger_attached_eq(inferior, true, "debugger should still appear attached");

  if (!shutdown_inferior(channel, inferior))
    return false;

  // When a process terminates it closes its exception channels.
  expect_debugger_attached_eq(inferior, false, "debugger should no longer appear attached");

  // Stop the waiter thread before closing the port that it's waiting on.
  join_wait_inf_thread(wait_inf_thread);

  detach_inferior(inferior_data, true);

  zx_handle_close(port);
  zx_handle_close(channel);
  zx_handle_close(inferior);

  END_TEST;
}

bool DebuggerThreadListTest() {
  BEGIN_TEST;

  springboard_t* sb;
  zx_handle_t inferior, channel;
  if (!setup_inferior(kTestInferiorChildName, &sb, &inferior, &channel))
    return false;

  zx_handle_t port = ZX_HANDLE_INVALID;
  EXPECT_EQ(zx_port_create(0, &port), ZX_OK);
  size_t max_threads = 10;
  inferior_data_t* inferior_data = attach_inferior(inferior, port, max_threads);
  thrd_t wait_inf_thread =
      start_wait_inf_thread(inferior_data, debugger_test_exception_handler, NULL);
  EXPECT_NE(port, ZX_HANDLE_INVALID);

  if (!start_inferior(sb))
    return false;
  if (!verify_inferior_running(channel))
    return false;

  send_simple_request(channel, RQST_START_LOOPING_THREADS);
  EXPECT_TRUE(recv_simple_response(channel, RESP_THREADS_STARTED), "");

  uint32_t buf_size = 100 * sizeof(zx_koid_t);
  size_t num_threads;
  zx_koid_t* threads = static_cast<zx_koid_t*>(malloc(buf_size));
  zx_status_t status =
      zx_object_get_info(inferior, ZX_INFO_PROCESS_THREADS, threads, buf_size, &num_threads, NULL);
  ASSERT_EQ(status, ZX_OK);

  // There should be at least 1+kNumExtraThreads threads in the result.
  ASSERT_GE(num_threads, 1 + kNumExtraThreads, "zx_object_get_info failed");

  // Verify each entry is valid.
  for (uint32_t i = 0; i < num_threads; ++i) {
    zx_koid_t koid = threads[i];
    unittest_printf("Looking up thread %llu\n", (long long)koid);
    zx_handle_t thread = ZX_HANDLE_INVALID;
    status = zx_object_get_child(inferior, koid, ZX_RIGHT_SAME_RIGHTS, &thread);
    EXPECT_EQ(status, ZX_OK, "zx_object_get_child failed");
    zx_info_handle_basic_t info;
    status = zx_object_get_info(thread, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
    EXPECT_EQ(status, ZX_OK, "zx_object_get_info failed");
    EXPECT_EQ(info.type, ZX_OBJ_TYPE_THREAD, "not a thread");
  }

  if (!shutdown_inferior(channel, inferior))
    return false;

  // Stop the waiter thread before closing the port that it's waiting on.
  join_wait_inf_thread(wait_inf_thread);

  detach_inferior(inferior_data, true);

  zx_handle_close(port);
  zx_handle_close(channel);
  zx_handle_close(inferior);

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

  // These are all dsos we link with. See BUILD.gn.
  const char* libc_so = "libc.so";
  bool found_libc = false;
  const char* unittest_so = "libunittest.so";
  bool found_unittest = false;

  const r_debug* debug = (r_debug*)debug_addr;
  const link_map* lmap = debug->r_map;

  EXPECT_EQ(debug->r_state, r_debug::RT_CONSISTENT);

  while (lmap != NULL) {
    if (strcmp(lmap->l_name, libc_so) == 0)
      found_libc = true;
    else if (strcmp(lmap->l_name, unittest_so) == 0)
      found_unittest = true;
    lmap = lmap->l_next;
  }

  EXPECT_TRUE(found_libc);
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

  // Exercise fxbug.dev/30693
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

}  // namespace

BEGIN_TEST_CASE(debugger_tests)
RUN_TEST(DebuggerTest)
RUN_TEST(DebuggerThreadListTest)
RUN_TEST(PropertyProcessDebugAddrTest)
RUN_TEST(WriteTextSegmentTest)
END_TEST_CASE(debugger_tests)
