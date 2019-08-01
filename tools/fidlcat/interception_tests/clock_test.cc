// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

std::string ClockExpected(time_t time, const char* format) {
  struct tm tm;
  char buffer[300];
  if (localtime_r(&time, &tm) == &tm) {
    strftime(buffer, sizeof(buffer), format, &tm);
  } else {
    buffer[0] = 0;
  }
  return std::string(buffer);
}

#define CLOCK_ADJUST_DISPLAY_TEST_CONTENT(result, expected)                                    \
  zx_handle_t handle = 0x12345678;                                                             \
  PerformDisplayTest(kClockAdjust,                                                             \
                     SystemCallTest::ZxClockAdjust(result, #result, handle, ZX_CLOCK_UTC, 10), \
                     expected);

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

#define kClockGetTestValue 1564175607533042989L

#define CLOCK_GET_DISPLAY_TEST_CONTENT(errno, expected)                                         \
  zx_time_t date = kClockGetTestValue;                                                          \
  PerformDisplayTest(kClockGet, SystemCallTest::ZxClockGet(errno, #errno, ZX_CLOCK_UTC, &date), \
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

#define CLOCK_GET_MONOTONIC_DISPLAY_TEST_CONTENT(result, expected)                             \
  PerformDisplayTest(kClockGetMonotonic, SystemCallTest::ZxClockGetMonotonic(result, #result), \
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

#define DEADLINE_AFTER_DISPLAY_TEST_CONTENT(result, nanoseconds, expected) \
  PerformDisplayTest(kDeadlineAfter,                                       \
                     SystemCallTest::ZxDeadlineAfter(result, #result, nanoseconds), expected);

#define DEADLINE_AFTER_DISPLAY_TEST(name, errno, nanoseconds, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                           \
    DEADLINE_AFTER_DISPLAY_TEST_CONTENT(errno, nanoseconds, expected);  \
  }                                                                     \
  TEST_F(InterceptionWorkflowTestArm, name) {                           \
    DEADLINE_AFTER_DISPLAY_TEST_CONTENT(errno, nanoseconds, expected);  \
  }

#define kDeadlineAfterTestValue 1564175607533042989L
#define kDeadlineAfterTestDuration 1000

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
