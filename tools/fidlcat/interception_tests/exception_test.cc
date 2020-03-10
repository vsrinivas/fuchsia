// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// Program stopped on exception tests.

#define DISPLAY_EXCEPTION_TEST_CONTENT(type, expected) PerformExceptionDisplayTest(type, expected);

#define DISPLAY_EXCEPTION_TEST(name, type, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { DISPLAY_EXCEPTION_TEST_CONTENT(type, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { DISPLAY_EXCEPTION_TEST_CONTENT(type, expected); }

DISPLAY_EXCEPTION_TEST(DisplayExceptionPageFault, debug_ipc::ExceptionType::kPageFault,
                       "\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
                       "at \x1B[31mfidlcat/main.cc\x1B[0m\x1B[103m:\x1B[34m10\x1B[0m\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
                       "at \x1B[31mfidlcat/foo.cc\x1B[0m\x1B[103m:\x1B[34m50\x1B[0m\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
                       "at \x1B[31mfidlcat/foo.cc\x1B[0m\x1B[103m:\x1B[34m25\x1B[0m\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[31m"
                       "thread stopped on exception\x1B[0m\n");

DISPLAY_EXCEPTION_TEST(DisplayExceptionGeneral, debug_ipc::ExceptionType::kGeneral,
                       "\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
                       "at \x1B[31mfidlcat/main.cc\x1B[0m\x1B[103m:\x1B[34m10\x1B[0m\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
                       "at \x1B[31mfidlcat/foo.cc\x1B[0m\x1B[103m:\x1B[34m50\x1B[0m\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
                       "at \x1B[31mfidlcat/foo.cc\x1B[0m\x1B[103m:\x1B[34m25\x1B[0m\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[31m"
                       "thread stopped on exception\x1B[0m\n");

// zx_exception_get_thread tests.

std::unique_ptr<SystemCallTest> ZxExceptionGetThread(int64_t result, std::string_view result_name,
                                                     zx_handle_t handle, zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_exception_get_thread", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define ZX_EXCEPTION_GET_THREAD_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out = kHandleOut;                                        \
  PerformDisplayTest("$plt(zx_exception_get_thread)",                  \
                     ZxExceptionGetThread(result, #result, kHandle, &out), expected);

#define ZX_EXCEPTION_GET_THREAD_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                       \
    ZX_EXCEPTION_GET_THREAD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                 \
  TEST_F(InterceptionWorkflowTestArm, name) {                       \
    ZX_EXCEPTION_GET_THREAD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

ZX_EXCEPTION_GET_THREAD_DISPLAY_TEST(
    ZxExceptionGetThread, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_exception_get_thread(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_exception_get_process tests.

std::unique_ptr<SystemCallTest> ZxExceptionGetProcess(int64_t result, std::string_view result_name,
                                                      zx_handle_t handle, zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_exception_get_process", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define ZX_EXCEPTION_GET_PROCESS_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out = kHandleOut;                                         \
  PerformDisplayTest("$plt(zx_exception_get_process)",                  \
                     ZxExceptionGetProcess(result, #result, kHandle, &out), expected);

#define ZX_EXCEPTION_GET_PROCESS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                        \
    ZX_EXCEPTION_GET_PROCESS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                  \
  TEST_F(InterceptionWorkflowTestArm, name) {                        \
    ZX_EXCEPTION_GET_PROCESS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

ZX_EXCEPTION_GET_PROCESS_DISPLAY_TEST(
    ZxExceptionGetProcess, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_exception_get_process(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

}  // namespace fidlcat
