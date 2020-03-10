// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

std::string ClockExpected(time_t time, const char* format) {
  struct tm tm;
  std::vector<char> buffer(2 * strlen(format));
  if (localtime_r(&time, &tm) == &tm) {
    strftime(buffer.data(), buffer.size(), format, &tm);
  } else {
    buffer[0] = 0;
  }
  return std::string(buffer.data());
}

// zx_clock_adjust tests.

std::unique_ptr<SystemCallTest> ZxClockAdjust(int64_t result, std::string_view result_name,
                                              zx_handle_t handle, zx_clock_t clock_id,
                                              int64_t offset) {
  auto value = std::make_unique<SystemCallTest>("zx_clock_adjust", result, result_name);
  value->AddInput(handle);
  value->AddInput(clock_id);
  value->AddInput(offset);
  return value;
}

#define CLOCK_ADJUST_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t handle = 0x12345678;                          \
  PerformDisplayTest("$plt(zx_clock_adjust)",               \
                     ZxClockAdjust(result, #result, handle, ZX_CLOCK_UTC, 10), expected);

#define CLOCK_ADJUST_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    CLOCK_ADJUST_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { CLOCK_ADJUST_DISPLAY_TEST_CONTENT(errno, expected); }

CLOCK_ADJUST_DISPLAY_TEST(ZxClockAdjust, ZX_OK,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_clock_adjust("
                          "handle:\x1B[32mhandle\x1B[0m: \x1B[31m12345678\x1B[0m, "
                          "clock_id:\x1B[32mclock\x1B[0m: \x1B[31mZX_CLOCK_UTC\x1B[0m, "
                          "offset:\x1B[32mint64\x1B[0m: \x1B[34m10\x1B[0m)\n"
                          "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_clock_get tests.

#define kClockGetTestValue 1564175607533042989L

std::unique_ptr<SystemCallTest> ZxClockGet(int64_t result, std::string_view result_name,
                                           zx_clock_t clock_id, zx_time_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_clock_get", result, result_name);
  value->AddInput(clock_id);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define CLOCK_GET_DISPLAY_TEST_CONTENT(errno, expected)                                    \
  zx_time_t date = 1564175607533042989;                                                    \
  PerformDisplayTest("$plt(zx_clock_get)", ZxClockGet(errno, #errno, ZX_CLOCK_UTC, &date), \
                     expected);

#define CLOCK_GET_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { CLOCK_GET_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { CLOCK_GET_DISPLAY_TEST_CONTENT(errno, expected); }

CLOCK_GET_DISPLAY_TEST(
    ZxClockGet, ZX_OK,
    ClockExpected(kClockGetTestValue / kOneBillion,
                  "\n"
                  "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_clock_get("
                  "clock_id:\x1B[32mclock\x1B[0m: \x1B[31mZX_CLOCK_UTC\x1B[0m)\n"
                  "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mtime\x1B[0m:"
                  " \x1B[34m%c and 533042989 ns\x1B[0m)\n")
        .c_str());

// zx_clock_get_monotonic tests.

std::unique_ptr<SystemCallTest> ZxClockGetMonotonic(int64_t result, std::string_view result_name) {
  return std::make_unique<SystemCallTest>("zx_clock_get_monotonic", result, result_name);
}

#define CLOCK_GET_MONOTONIC_DISPLAY_TEST_CONTENT(result, expected)                         \
  PerformDisplayTest("$plt(zx_clock_get_monotonic)", ZxClockGetMonotonic(result, #result), \
                     expected);

#define CLOCK_GET_MONOTONIC_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    CLOCK_GET_MONOTONIC_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    CLOCK_GET_MONOTONIC_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

#define kClockGetMonotonicTestValue 164056115697412L

CLOCK_GET_MONOTONIC_DISPLAY_TEST(
    ZxClockGetMonotonic, kClockGetMonotonicTestValue,
    ClockExpected(kClockGetMonotonicTestValue / kOneBillion,
                  "\n"
                  "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_clock_get_monotonic()\n"
                  "  -> \x1B[32mtime\x1B[0m: \x1B[34m%c and 115697412 ns\x1B[0m\n")
        .c_str());

// zx_deadline_after tests.

#define kDeadlineAfterTestValue 1564175607533042989L
#define kDeadlineAfterTestDuration 1000

std::unique_ptr<SystemCallTest> ZxDeadlineAfter(int64_t result, std::string_view result_name,
                                                zx_time_t nanoseconds) {
  auto value = std::make_unique<SystemCallTest>("zx_deadline_after", result, result_name);
  value->AddInput(nanoseconds);
  return value;
}

#define DEADLINE_AFTER_DISPLAY_TEST_CONTENT(result, nanoseconds, expected)                     \
  PerformDisplayTest("$plt(zx_deadline_after)", ZxDeadlineAfter(result, #result, nanoseconds), \
                     expected);

#define DEADLINE_AFTER_DISPLAY_TEST(name, errno, nanoseconds, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                           \
    DEADLINE_AFTER_DISPLAY_TEST_CONTENT(errno, nanoseconds, expected);  \
  }                                                                     \
  TEST_F(InterceptionWorkflowTestArm, name) {                           \
    DEADLINE_AFTER_DISPLAY_TEST_CONTENT(errno, nanoseconds, expected);  \
  }

DEADLINE_AFTER_DISPLAY_TEST(
    ZxDeadlineAfter, kDeadlineAfterTestValue, kDeadlineAfterTestDuration,
    ClockExpected(kDeadlineAfterTestValue / kOneBillion,
                  "\n"
                  "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_deadline_after("
                  "nanoseconds:\x1B[32mduration\x1B[0m: \x1B[34m1000 nano seconds\x1B[0m)\n"
                  "  -> \x1B[32mtime\x1B[0m: \x1B[34m%c and 533042989 ns\x1B[0m\n")
        .c_str());

DEADLINE_AFTER_DISPLAY_TEST(
    ZxDeadlineAfterInfinite, ZX_TIME_INFINITE, ZX_TIME_INFINITE,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_deadline_after("
    "nanoseconds:\x1B[32mduration\x1B[0m: \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
    "  -> \x1B[32mtime\x1B[0m: \x1B[34mZX_TIME_INFINITE\x1B[0m\n");

}  // namespace fidlcat
