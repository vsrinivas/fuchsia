// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_task_suspend tests.

std::unique_ptr<SystemCallTest> ZxTaskSuspend(int64_t result, std::string_view result_name,
                                              zx_handle_t handle, zx_handle_t* token) {
  auto value = std::make_unique<SystemCallTest>("zx_task_suspend", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(token));
  return value;
}

#define TASK_SUSPEND_DISPLAY_TEST_CONTENT(result, expected)                                    \
  zx_handle_t token = kHandleOut;                                                              \
  PerformDisplayTest("$plt(zx_task_suspend)", ZxTaskSuspend(result, #result, kHandle, &token), \
                     expected);

#define TASK_SUSPEND_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    TASK_SUSPEND_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { TASK_SUSPEND_DISPLAY_TEST_CONTENT(errno, expected); }

TASK_SUSPEND_DISPLAY_TEST(
    ZxTaskSuspend, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_task_suspend(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (token:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_task_suspend_token tests.

std::unique_ptr<SystemCallTest> ZxTaskSuspendToken(int64_t result, std::string_view result_name,
                                                   zx_handle_t handle, zx_handle_t* token) {
  auto value = std::make_unique<SystemCallTest>("zx_task_suspend_token", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(token));
  return value;
}

#define TASK_SUSPEND_TOKEN_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t token = kHandleOut;                                 \
  PerformDisplayTest("$plt(zx_task_suspend_token)",               \
                     ZxTaskSuspendToken(result, #result, kHandle, &token), expected);

#define TASK_SUSPEND_TOKEN_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    TASK_SUSPEND_TOKEN_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    TASK_SUSPEND_TOKEN_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

TASK_SUSPEND_TOKEN_DISPLAY_TEST(
    ZxTaskSuspendToken, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_task_suspend_token(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (token:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_task_create_exception_channel tests.

std::unique_ptr<SystemCallTest> ZxTaskCreateExceptionChannel(int64_t result,
                                                             std::string_view result_name,
                                                             zx_handle_t handle, uint32_t options,
                                                             zx_handle_t* out) {
  auto value =
      std::make_unique<SystemCallTest>("zx_task_create_exception_channel", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define TASK_CREATE_EXCEPTION_CHANNEL_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out = kHandleOut;                                              \
  PerformDisplayTest("$plt(zx_task_create_exception_channel)",               \
                     ZxTaskCreateExceptionChannel(result, #result, kHandle, 0, &out), expected);

#define TASK_CREATE_EXCEPTION_CHANNEL_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                             \
    TASK_CREATE_EXCEPTION_CHANNEL_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) {                             \
    TASK_CREATE_EXCEPTION_CHANNEL_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

TASK_CREATE_EXCEPTION_CHANNEL_DISPLAY_TEST(
    ZxTaskCreateExceptionChannel, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_task_create_exception_channel("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_task_kill tests.

std::unique_ptr<SystemCallTest> ZxTaskKill(int64_t result, std::string_view result_name,
                                           zx_handle_t handle) {
  auto value = std::make_unique<SystemCallTest>("zx_task_kill", result, result_name);
  value->AddInput(handle);
  return value;
}

#define TASK_KILL_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_task_kill)", ZxTaskKill(result, #result, kHandle), expected);

#define TASK_KILL_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { TASK_KILL_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { TASK_KILL_DISPLAY_TEST_CONTENT(errno, expected); }

TASK_KILL_DISPLAY_TEST(ZxTaskKill, ZX_OK,
                       "\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                       "zx_task_kill(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
                       "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
