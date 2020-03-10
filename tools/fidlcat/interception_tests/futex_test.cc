// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_futex_wait tests.

std::unique_ptr<SystemCallTest> ZxFutexWait(int64_t result, std::string_view result_name,
                                            const zx_futex_t* value_ptr, zx_futex_t current_value,
                                            zx_handle_t new_futex_owner, zx_time_t deadline) {
  auto value = std::make_unique<SystemCallTest>("zx_futex_wait", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(value_ptr));
  value->AddInput(current_value);
  value->AddInput(new_futex_owner);
  value->AddInput(deadline);
  return value;
}

#define FUTEX_WAIT_DISPLAY_TEST_CONTENT(result, expected)                                    \
  zx_futex_t value = kFutex;                                                                 \
  PerformDisplayTest("$plt(zx_futex_wait)",                                                  \
                     ZxFutexWait(result, #result, &value, value, kHandle, ZX_TIME_INFINITE), \
                     expected);

#define FUTEX_WAIT_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { FUTEX_WAIT_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { FUTEX_WAIT_DISPLAY_TEST_CONTENT(errno, expected); }

FUTEX_WAIT_DISPLAY_TEST(ZxFutexWait, ZX_OK,
                        "\n"
                        "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                        "zx_futex_wait("
                        "value_ptr:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m56789\x1B[0m, "
                        "current_value:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m56789\x1B[0m, "
                        "new_futex_owner:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                        "deadline:\x1B[32mtime\x1B[0m: \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
                        "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_futex_wake tests.

std::unique_ptr<SystemCallTest> ZxFutexWake(int64_t result, std::string_view result_name,
                                            const zx_futex_t* value_ptr, uint32_t wake_count) {
  auto value = std::make_unique<SystemCallTest>("zx_futex_wake", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(value_ptr));
  value->AddInput(wake_count);
  return value;
}

#define FUTEX_WAKE_DISPLAY_TEST_CONTENT(result, expected) \
  zx_futex_t value = kFutex;                              \
  PerformDisplayTest("$plt(zx_futex_wake)", ZxFutexWake(result, #result, &value, 3), expected);

#define FUTEX_WAKE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { FUTEX_WAKE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { FUTEX_WAKE_DISPLAY_TEST_CONTENT(errno, expected); }

FUTEX_WAKE_DISPLAY_TEST(ZxFutexWake, ZX_OK,
                        "\n"
                        "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                        "zx_futex_wake("
                        "value_ptr:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m56789\x1B[0m, "
                        "wake_count:\x1B[32muint32\x1B[0m: \x1B[34m3\x1B[0m)\n"
                        "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_futex_requeue tests.

std::unique_ptr<SystemCallTest> ZxFutexRequeue(int64_t result, std::string_view result_name,
                                               const zx_futex_t* value_ptr, uint32_t wake_count,
                                               zx_futex_t current_value,
                                               const zx_futex_t* requeue_ptr,
                                               uint32_t requeue_count,
                                               zx_handle_t new_requeue_owner) {
  auto value = std::make_unique<SystemCallTest>("zx_futex_requeue", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(value_ptr));
  value->AddInput(wake_count);
  value->AddInput(current_value);
  value->AddInput(reinterpret_cast<uint64_t>(requeue_ptr));
  value->AddInput(requeue_count);
  value->AddInput(new_requeue_owner);
  return value;
}

#define FUTEX_REQUEUE_DISPLAY_TEST_CONTENT(result, expected)                                  \
  zx_futex_t value = kFutex;                                                                  \
  zx_futex_t requeue = kFutex2;                                                               \
  PerformDisplayTest("$plt(zx_futex_requeue)",                                                \
                     ZxFutexRequeue(result, #result, &value, 2, value, &requeue, 3, kHandle), \
                     expected);

#define FUTEX_REQUEUE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {             \
    FUTEX_REQUEUE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) { FUTEX_REQUEUE_DISPLAY_TEST_CONTENT(errno, expected); }

FUTEX_REQUEUE_DISPLAY_TEST(ZxFutexRequeue, ZX_OK,
                           "\n"
                           "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                           "zx_futex_requeue("
                           "value_ptr:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m56789\x1B[0m, "
                           "wake_count:\x1B[32muint32\x1B[0m: \x1B[34m2\x1B[0m, "
                           "current_value:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m56789\x1B[0m, "
                           "requeue_ptr:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m98765\x1B[0m, "
                           "requeue_count:\x1B[32muint32\x1B[0m: \x1B[34m3\x1B[0m, "
                           "new_requeue_owner:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
                           "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_futex_wake_single_owner tests.

std::unique_ptr<SystemCallTest> ZxFutexWakeSingleOwner(int64_t result, std::string_view result_name,
                                                       const zx_futex_t* value_ptr) {
  auto value = std::make_unique<SystemCallTest>("zx_futex_wake_single_owner", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(value_ptr));
  return value;
}

#define FUTEX_WAKE_SINGLE_OWNER_DISPLAY_TEST_CONTENT(result, expected) \
  zx_futex_t value = kFutex;                                           \
  PerformDisplayTest("$plt(zx_futex_wake_single_owner)",               \
                     ZxFutexWakeSingleOwner(result, #result, &value), expected);

#define FUTEX_WAKE_SINGLE_OWNER_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                       \
    FUTEX_WAKE_SINGLE_OWNER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                 \
  TEST_F(InterceptionWorkflowTestArm, name) {                       \
    FUTEX_WAKE_SINGLE_OWNER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

FUTEX_WAKE_SINGLE_OWNER_DISPLAY_TEST(
    ZxFutexWakeSingleOwner, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_futex_wake_single_owner(value_ptr:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m56789\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_futex_requeue_single_owner tests.

std::unique_ptr<SystemCallTest> ZxFutexRequeueSingleOwner(
    int64_t result, std::string_view result_name, const zx_futex_t* value_ptr,
    zx_futex_t current_value, const zx_futex_t* requeue_ptr, uint32_t requeue_count,
    zx_handle_t new_requeue_owner) {
  auto value =
      std::make_unique<SystemCallTest>("zx_futex_requeue_single_owner", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(value_ptr));
  value->AddInput(current_value);
  value->AddInput(reinterpret_cast<uint64_t>(requeue_ptr));
  value->AddInput(requeue_count);
  value->AddInput(new_requeue_owner);
  return value;
}

#define FUTEX_REQUEUE_SINGLE_OWNER_DISPLAY_TEST_CONTENT(result, expected) \
  zx_futex_t value = kFutex;                                              \
  zx_futex_t requeue = kFutex2;                                           \
  PerformDisplayTest(                                                     \
      "$plt(zx_futex_requeue_single_owner)",                              \
      ZxFutexRequeueSingleOwner(result, #result, &value, value, &requeue, 3, kHandle), expected);

#define FUTEX_REQUEUE_SINGLE_OWNER_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                          \
    FUTEX_REQUEUE_SINGLE_OWNER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                    \
  TEST_F(InterceptionWorkflowTestArm, name) {                          \
    FUTEX_REQUEUE_SINGLE_OWNER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

FUTEX_REQUEUE_SINGLE_OWNER_DISPLAY_TEST(
    ZxFutexRequeueSingleOwner, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_futex_requeue_single_owner("
    "value_ptr:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m56789\x1B[0m, "
    "current_value:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m56789\x1B[0m, "
    "requeue_ptr:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m98765\x1B[0m, "
    "requeue_count:\x1B[32muint32\x1B[0m: \x1B[34m3\x1B[0m, "
    "new_requeue_owner:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_futex_get_owner tests.

std::unique_ptr<SystemCallTest> ZxFutexGetOwner(int64_t result, std::string_view result_name,
                                                const zx_futex_t* value_ptr, zx_koid_t* koid) {
  auto value = std::make_unique<SystemCallTest>("zx_futex_get_owner", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(value_ptr));
  value->AddInput(reinterpret_cast<uint64_t>(koid));
  return value;
}

#define FUTEX_GET_OWNER_DISPLAY_TEST_CONTENT(result, expected)                                    \
  zx_futex_t value = kFutex;                                                                      \
  zx_koid_t koid = kKoid;                                                                         \
  PerformDisplayTest("$plt(zx_futex_get_owner)", ZxFutexGetOwner(result, #result, &value, &koid), \
                     expected);

#define FUTEX_GET_OWNER_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {               \
    FUTEX_GET_OWNER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {               \
    FUTEX_GET_OWNER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

FUTEX_GET_OWNER_DISPLAY_TEST(
    ZxFutexGetOwner, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_futex_get_owner(value_ptr:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m56789\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (koid:\x1B[32mzx_koid_t\x1B[0m: \x1B[31m4252\x1B[0m)\n");

// zx_futex_wake_handle_close_thread_exit tests.

std::unique_ptr<SystemCallTest> ZxFutexWakeHandleCloseThreadExit(
    int64_t result, std::string_view result_name, const zx_futex_t* value_ptr, uint32_t wake_count,
    int32_t new_value, zx_handle_t close_handle) {
  auto value = std::make_unique<SystemCallTest>("zx_futex_wake_handle_close_thread_exit", result,
                                                result_name);
  value->AddInput(reinterpret_cast<uint64_t>(value_ptr));
  value->AddInput(wake_count);
  value->AddInput(new_value);
  value->AddInput(close_handle);
  return value;
}

#define FUTEX_WAKE_HANDLE_CLOSE_THREAD_EXIT_DISPLAY_TEST_CONTENT(result, expected) \
  zx_futex_t value = kFutex;                                                       \
  PerformNoReturnDisplayTest(                                                      \
      "$plt(zx_futex_wake_handle_close_thread_exit)",                              \
      ZxFutexWakeHandleCloseThreadExit(result, #result, &value, 2, -1, kHandle), expected);

#define FUTEX_WAKE_HANDLE_CLOSE_THREAD_EXIT_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                   \
    FUTEX_WAKE_HANDLE_CLOSE_THREAD_EXIT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                                   \
    FUTEX_WAKE_HANDLE_CLOSE_THREAD_EXIT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

FUTEX_WAKE_HANDLE_CLOSE_THREAD_EXIT_DISPLAY_TEST(
    ZxFutexWakeHandleCloseThreadExit, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_futex_wake_handle_close_thread_exit("
    "value_ptr:\x1B[32mzx_futex_t\x1B[0m: \x1B[31m56789\x1B[0m, "
    "wake_count:\x1B[32muint32\x1B[0m: \x1B[34m2\x1B[0m, "
    "new_value:\x1B[32mint32\x1B[0m: \x1B[34m-1\x1B[0m, "
    "close_handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n");

}  // namespace fidlcat
