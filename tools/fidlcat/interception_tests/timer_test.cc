// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_timer_create tests.

std::unique_ptr<SystemCallTest> ZxTimerCreate(int64_t result, std::string_view result_name,
                                              uint32_t options, zx_clock_t clock_id,
                                              zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_timer_create", result, result_name);
  value->AddInput(options);
  value->AddInput(clock_id);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

// Checks that we can decode a zx_timer_create syscall.
// Also checks that we create the right semantic for the timers.
#define TIMER_CREATE_DISPLAY_TEST_CONTENT(result, expected)                                  \
  zx_handle_t out = kHandleOut;                                                              \
  ProcessController controller(this, session(), loop());                                     \
  PerformDisplayTest(&controller, "$plt(zx_timer_create)",                                   \
                     ZxTimerCreate(result, #result, 0, ZX_CLOCK_MONOTONIC, &out), expected); \
  SyscallDecoderDispatcher* dispatcher = controller.workflow().syscall_decoder_dispatcher(); \
  const fidl_codec::semantic::HandleDescription* description0 =                              \
      dispatcher->inference().GetHandleDescription(kFirstPid, out);                          \
  ASSERT_NE(description0, nullptr);                                                          \
  ASSERT_EQ(description0->type(), "timer");                                                  \
  ASSERT_EQ(description0->fd(), 0);                                                          \
  const fidl_codec::semantic::HandleDescription* description1 =                              \
      dispatcher->inference().GetHandleDescription(kSecondPid, out);                         \
  ASSERT_NE(description1, nullptr);                                                          \
  ASSERT_EQ(description1->type(), "timer");                                                  \
  ASSERT_EQ(description1->fd(), 1);

#define TIMER_CREATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    TIMER_CREATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { TIMER_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

TIMER_CREATE_DISPLAY_TEST(
    ZxTimerCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_timer_create("
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "clock_id:\x1B[32mclock\x1B[0m: \x1B[31mZX_CLOCK_MONOTONIC\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_timer_set tests.

std::unique_ptr<SystemCallTest> ZxTimerSet(int64_t result, std::string_view result_name,
                                           zx_handle_t handle, zx_time_t deadline,
                                           zx_duration_t slack) {
  auto value = std::make_unique<SystemCallTest>("zx_timer_set", result, result_name);
  value->AddInput(handle);
  value->AddInput(deadline);
  value->AddInput(slack);
  return value;
}

#define TIMER_SET_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_timer_set)",               \
                     ZxTimerSet(result, #result, kHandle, ZX_MSEC(123), ZX_USEC(1)), expected);

#define TIMER_SET_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { TIMER_SET_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { TIMER_SET_DISPLAY_TEST_CONTENT(errno, expected); }

TIMER_SET_DISPLAY_TEST(ZxTimerSet, ZX_OK,
                       "\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                       "zx_timer_set("
                       "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                       "deadline:\x1B[32mzx_time_t\x1B[0m: \x1B[34m123000000 nano seconds\x1B[0m, "
                       "slack:\x1B[32mduration\x1B[0m: \x1B[34m1000 nano seconds\x1B[0m)\n"
                       "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_timer_cancel tests.

std::unique_ptr<SystemCallTest> ZxTimerCancel(int64_t result, std::string_view result_name,
                                              zx_handle_t handle) {
  auto value = std::make_unique<SystemCallTest>("zx_timer_cancel", result, result_name);
  value->AddInput(handle);
  return value;
}

#define TIMER_CANCEL_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_timer_cancel)", ZxTimerCancel(result, #result, kHandle), expected);

#define TIMER_CANCEL_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    TIMER_CANCEL_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { TIMER_CANCEL_DISPLAY_TEST_CONTENT(errno, expected); }

TIMER_CANCEL_DISPLAY_TEST(ZxTimerCancel, ZX_OK,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                          "zx_timer_cancel(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
                          "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
