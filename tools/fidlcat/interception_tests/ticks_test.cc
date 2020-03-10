// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_ticks_get tests.

std::unique_ptr<SystemCallTest> ZxTicksGet(int64_t result, std::string_view result_name) {
  return std::make_unique<SystemCallTest>("zx_ticks_get", result, result_name);
}

#define TICKS_GET_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_ticks_get)", ZxTicksGet(result, #result), expected);

#define TICKS_GET_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { TICKS_GET_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { TICKS_GET_DISPLAY_TEST_CONTENT(errno, expected); }

TICKS_GET_DISPLAY_TEST(ZxTicksGet, 497475301988264,
                       "\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_ticks_get()\n"
                       "  -> \x1B[32mticks\x1B[0m: \x1B[34m497475301988264\x1B[0m\n");

// zx_ticks_per_second tests.

std::unique_ptr<SystemCallTest> ZxTicksPerSecond(int64_t result, std::string_view result_name) {
  return std::make_unique<SystemCallTest>("zx_ticks_per_second", result, result_name);
}

#define TICKS_PER_SECOND_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_ticks_per_second)", ZxTicksPerSecond(result, #result), expected);

#define TICKS_PER_SECOND_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    TICKS_PER_SECOND_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    TICKS_PER_SECOND_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

TICKS_PER_SECOND_DISPLAY_TEST(
    ZxTicksPerSecond, 2992964000,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_ticks_per_second()\n"
    "  -> \x1B[32mticks\x1B[0m: \x1B[34m2992964000\x1B[0m\n");

}  // namespace fidlcat
