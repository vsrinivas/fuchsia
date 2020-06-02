// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/exception.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/syscalls/port.h>

#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#include "debugger.h"
#include "inferior-control.h"
#include "inferior.h"
#include "utils.h"

namespace {

struct suspend_on_start_test_state_t {
  int started_threads = 0;
};

bool suspend_on_start_test_handler(inferior_data_t* data, const zx_port_packet_t* packet,
                                   void* handler_arg) {
  auto* test_state = reinterpret_cast<suspend_on_start_test_state_t*>(handler_arg);
  BEGIN_HELPER;

  // This test is supposed to only get an exception and nothing else.
  zx_info_handle_basic_t basic_info;
  zx_status_t status = zx_object_get_info(data->exception_channel, ZX_INFO_HANDLE_BASIC,
                                          &basic_info, sizeof(basic_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(basic_info.koid, packet->key);

  zx::exception exception;
  zx_exception_info_t info;
  uint32_t num_bytes = sizeof(info);
  uint32_t num_handles = 1;
  status = zx_channel_read(data->exception_channel, 0, &info, exception.reset_and_get_address(),
                           num_bytes, num_handles, nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);

  switch (info.type) {
    case ZX_EXCP_THREAD_STARTING:
      unittest_printf("thread %lu starting\n", info.tid);
      // We only want to resume the first thread.
      test_state->started_threads++;
      break;
    case ZX_EXCP_THREAD_EXITING:
      unittest_printf("thread %lu exiting\n", info.tid);
      ASSERT_TRUE(handle_thread_exiting(data->inferior, &info, std::move(exception)));
      break;
    default:
      unittest_printf("Unexpected exception %s (%u) on thread %lu\n",
                      tu_exception_to_string(info.type), info.type, info.tid);
      break;
  }

  END_HELPER;
}

}  // namespace

bool SuspendOnStartTest() {
  BEGIN_TEST;

  springboard_t* sb;
  zx_handle_t inferior, channel;
  if (!setup_inferior(kTestSuspendOnStart, &sb, &inferior, &channel))
    return false;

  // Attach to the inferior now because we want to see thread starting
  // exceptions.
  zx_handle_t port = ZX_HANDLE_INVALID;
  EXPECT_EQ(zx_port_create(0, &port), ZX_OK);
  size_t max_threads = 2;
  inferior_data_t* inferior_data = attach_inferior(inferior, port, max_threads);

  suspend_on_start_test_state_t test_state = {};
  thrd_t wait_inf_thread =
      start_wait_inf_thread(inferior_data, suspend_on_start_test_handler, &test_state);
  EXPECT_NE(port, ZX_HANDLE_INVALID);

  if (!start_inferior(sb))
    return false;

  // The remaining testing happens at this point as threads start.
  // This testing is done in |suspend_on_start_test_handler()|.

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

BEGIN_TEST_CASE(suspend_on_start_tests)
RUN_TEST(SuspendOnStartTest);
END_TEST_CASE(suspend_on_start_tests)
