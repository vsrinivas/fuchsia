// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

#define DISPLAY_EXCEPTION_TEST_CONTENT(expected) PerformExceptionDisplayTest(expected);

#define DISPLAY_EXCEPTION_TEST(name, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { DISPLAY_EXCEPTION_TEST_CONTENT(expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { DISPLAY_EXCEPTION_TEST_CONTENT(expected); }

DISPLAY_EXCEPTION_TEST(DisplayException,
                       "\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
                       "at \x1B[31mfidlcat/main.cc\x1B[0m\x1B[103m:\x1B[34m10\x1B[0m\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
                       "at \x1B[31mfidlcat/foo.cc\x1B[0m\x1B[103m:\x1B[34m50\x1B[0m\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
                       "at \x1B[31mfidlcat/foo.cc\x1B[0m\x1B[103m:\x1B[34m25\x1B[0m\n"
                       "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[31m"
                       "thread stopped on exception\x1B[0m\n");

}  // namespace fidlcat
