// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_task_bind_exception_port tests.

std::unique_ptr<SystemCallTest> ZxTaskBindExceptionPort(int64_t result,
                                                        std::string_view result_name,
                                                        zx_handle_t handle, zx_handle_t port,
                                                        uint64_t key, uint32_t options) {
  auto value = std::make_unique<SystemCallTest>("zx_task_bind_exception_port", result, result_name);
  value->AddInput(handle);
  value->AddInput(port);
  value->AddInput(key);
  value->AddInput(options);
  return value;
}

#define TASK_BIND_EXCEPTION_PORT_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("zx_task_bind_exception_port@plt",                 \
                     ZxTaskBindExceptionPort(result, #result, kHandle, kPort, kKey, 0), expected);

#define TASK_BIND_EXCEPTION_PORT_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                        \
    TASK_BIND_EXCEPTION_PORT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                  \
  TEST_F(InterceptionWorkflowTestArm, name) {                        \
    TASK_BIND_EXCEPTION_PORT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

TASK_BIND_EXCEPTION_PORT_DISPLAY_TEST(ZxTaskBindExceptionPort, ZX_OK,
                                      "\n"
                                      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                                      "zx_task_bind_exception_port("
                                      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                                      "port:\x1B[32mhandle\x1B[0m: \x1B[31mdf0b2ec1\x1B[0m, "
                                      "key:\x1B[32muint64\x1B[0m: \x1B[34m1234\x1B[0m, "
                                      "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                                      "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_task_suspend tests.

std::unique_ptr<SystemCallTest> ZxTaskSuspend(int64_t result, std::string_view result_name,
                                              zx_handle_t handle, zx_handle_t* token) {
  auto value = std::make_unique<SystemCallTest>("zx_task_suspend", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(token));
  return value;
}

#define TASK_SUSPEND_DISPLAY_TEST_CONTENT(result, expected)                                  \
  zx_handle_t token = kHandleOut;                                                            \
  PerformDisplayTest("zx_task_suspend@plt", ZxTaskSuspend(result, #result, kHandle, &token), \
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
  PerformDisplayTest("zx_task_suspend_token@plt",                 \
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

// zx_task_resume_from_exception tests.

std::unique_ptr<SystemCallTest> ZxTaskResumeFromException(int64_t result,
                                                          std::string_view result_name,
                                                          zx_handle_t handle, zx_handle_t port,
                                                          uint32_t options) {
  auto value =
      std::make_unique<SystemCallTest>("zx_task_resume_from_exception", result, result_name);
  value->AddInput(handle);
  value->AddInput(port);
  value->AddInput(options);
  return value;
}

#define TASK_RESUME_FROM_EXCEPTION_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("zx_task_resume_from_exception@plt",                 \
                     ZxTaskResumeFromException(result, #result, kHandle, kPort, 0), expected);

#define TASK_RESUME_FROM_EXCEPTION_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                          \
    TASK_RESUME_FROM_EXCEPTION_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                    \
  TEST_F(InterceptionWorkflowTestArm, name) {                          \
    TASK_RESUME_FROM_EXCEPTION_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

TASK_RESUME_FROM_EXCEPTION_DISPLAY_TEST(ZxTaskResumeFromException, ZX_OK,
                                        "\n"
                                        "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                                        "zx_task_resume_from_exception("
                                        "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                                        "port:\x1B[32mhandle\x1B[0m: \x1B[31mdf0b2ec1\x1B[0m, "
                                        "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                                        "  -> \x1B[32mZX_OK\x1B[0m\n");

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
  PerformDisplayTest("zx_task_create_exception_channel@plt",                 \
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
  PerformDisplayTest("zx_task_kill@plt", ZxTaskKill(result, #result, kHandle), expected);

#define TASK_KILL_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { TASK_KILL_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { TASK_KILL_DISPLAY_TEST_CONTENT(errno, expected); }

TASK_KILL_DISPLAY_TEST(ZxTaskKill, ZX_OK,
                       "\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                       "zx_task_kill(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
                       "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
