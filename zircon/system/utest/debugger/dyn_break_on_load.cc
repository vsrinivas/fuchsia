// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/status.h>
#include <zircon/syscalls/port.h>

#include <link.h>

#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#include "inferior-control.h"
#include "inferior.h"
#include "utils.h"

namespace {

struct dyn_break_on_load_state_t {
  zx_handle_t process_handle = ZX_HANDLE_INVALID;
  int dyn_load_count = 0;
};

bool dyn_break_on_load_test_handler(inferior_data_t* data, const zx_port_packet_t* packet,
                                    void* handler_arg) {
  auto* test_state = reinterpret_cast<dyn_break_on_load_state_t*>(handler_arg);
  BEGIN_HELPER;

  // This test is supposed to only get an exception and nothing else.
  ASSERT_EQ(tu_get_koid(data->exception_channel), packet->key);
  auto [info, raw_exception] = tu_read_exception(data->exception_channel);
  zx::exception exception(raw_exception);

  switch (info.type) {
    case ZX_EXCP_SW_BREAKPOINT: {
      unittest_printf("Got ld.so breakpoint.\n");
      test_state->dyn_load_count++;

      // Get the debug break address
      uintptr_t r_debug_address;
      zx_status_t status = zx_object_get_property(test_state->process_handle,
                                                  ZX_PROP_PROCESS_DEBUG_ADDR,
                                                  &r_debug_address, sizeof(r_debug_address));
      ASSERT_EQ(status, ZX_OK);

      size_t actual = 0;
      r_debug dl_debug = {};
      status = zx_process_read_memory(test_state->process_handle, r_debug_address, &dl_debug,
                                      sizeof(dl_debug), &actual);
      ASSERT_EQ(status, ZX_OK);
      ASSERT_EQ(actual, sizeof(dl_debug));

      // Get the registers.
      zx::thread thread;
      status = exception.get_thread(&thread);
      ASSERT_EQ(status, ZX_OK);

      zx_thread_state_general_regs_t regs = {};
      read_inferior_gregs(thread.get(), &regs);

      uint64_t rip = 0;
#if defined(__x86_64__)
      // x64 will report the exception address after execution the software breakpoint instruction.
      rip = regs.rip - 1;
#elif defined(__aarch64__)
      rip = regs.pc;
#endif

      // The breakpoint should be exactly the same as informed by the dynamic loader.
      ASSERT_EQ(rip, dl_debug.r_brk_on_load);

      ASSERT_EQ(tu_cleanup_breakpoint(thread.get()), ZX_OK);

      break;
    }
    default:
      unittest_printf("Unexpected exception %s (%u) on thread %lu\n",
                      tu_exception_to_string(info.type), info.type, info.tid);
      break;
  }

  tu_resume_from_exception(exception.get());

  END_HELPER;
}

bool DynBreakOnLoadTest() {
  BEGIN_TEST;

  springboard_t* sb;
  zx_handle_t inferior, channel;
  if (!setup_inferior(kTestDynBreakOnLoad, &sb, &inferior, &channel))
    return false;

  dyn_break_on_load_state_t test_state = {};
  test_state.process_handle = inferior;

  const uintptr_t kBreakOnLoad = 1;
  zx_status_t status = zx_object_set_property(inferior, ZX_PROP_PROCESS_BREAK_ON_LOAD,
                                              &kBreakOnLoad, sizeof(kBreakOnLoad));
  if (status != ZX_OK) {
    fprintf(stderr, "Could not set dynamic linker break on load property: %s\n",
            zx_status_get_string(status));
    ASSERT_EQ(status, ZX_OK);
  }

  // Attach to the inferior now because we want to see thread starting exceptions.
  zx_handle_t port = tu_io_port_create();
  size_t max_threads = 2;
  inferior_data_t* inferior_data = attach_inferior(inferior, port, max_threads);

  thrd_t wait_inf_thread =
      start_wait_inf_thread(inferior_data, dyn_break_on_load_test_handler, &test_state);
  EXPECT_NE(port, ZX_HANDLE_INVALID);

  if (!start_inferior(sb))
    return false;

  // The remaining testing happens at this point as threads start.
  // This testing is done in |dyn_break_on_load_test_handler()|.

  if (!shutdown_inferior(channel, inferior))
    return false;

  // Stop the waiter thread before closing the port that it's waiting on.
  join_wait_inf_thread(wait_inf_thread);

  detach_inferior(inferior_data, true);

  tu_handle_close(port);
  tu_handle_close(channel);
  tu_handle_close(inferior);

  // Verify how many loads there were.
  ASSERT_EQ(test_state.dyn_load_count, 10);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(dyn_break_on_load_tests)
RUN_TEST(DynBreakOnLoadTest);
END_TEST_CASE(dyn_break_on_load_tests)
