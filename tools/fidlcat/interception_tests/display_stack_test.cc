// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

std::unique_ptr<SystemCallTest> ZxChannelCreate(int64_t result, std::string_view result_name,
                                                uint32_t options, zx_handle_t* out0,
                                                zx_handle_t* out1);

#define DISPLAY_STACK_TEST_CONTENT(errno, level, expected)                                     \
  decode_options_.stack_level = level;                                                         \
  zx_handle_t out0 = 0x12345678;                                                               \
  zx_handle_t out1 = 0x87654321;                                                               \
  PerformDisplayTest("zx_channel_create@plt", ZxChannelCreate(errno, #errno, 0, &out0, &out1), \
                     expected);

#define DISPLAY_STACK_TEST(name, errno, level, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    DISPLAY_STACK_TEST_CONTENT(errno, level, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { DISPLAY_STACK_TEST_CONTENT(errno, level, expected); }

DISPLAY_STACK_TEST(
    DisplayNoStack, ZX_OK, kNoStack,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_create("
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out0:\x1B[32mhandle\x1B[0m: \x1B[31m12345678\x1B[0m, "
    "out1:\x1B[32mhandle\x1B[0m: \x1B[31m87654321\x1B[0m)\n");

DISPLAY_STACK_TEST(
    DisplayPartialStack, ZX_OK, kPartialStack,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
    "at \x1B[31mfidlcat/main.cc\x1B[0m\x1B[103m:\x1B[34m10\x1B[0m\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \x1B[103m"
    "at \x1B[31mfidlcat/foo.cc\x1B[0m\x1B[103m:\x1B[34m50\x1B[0m\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_create("
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out0:\x1B[32mhandle\x1B[0m: \x1B[31m12345678\x1B[0m, "
    "out1:\x1B[32mhandle\x1B[0m: \x1B[31m87654321\x1B[0m)\n");

#define BAD_STACK_TEST_CONTENT(errno, expected)                                                \
  set_bad_stack();                                                                             \
  zx_handle_t out0 = 0x12345678;                                                               \
  zx_handle_t out1 = 0x87654321;                                                               \
  PerformAbortedTest("zx_channel_create@plt", ZxChannelCreate(errno, #errno, 0, &out0, &out1), \
                     expected);

#define BAD_STACK_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { BAD_STACK_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { BAD_STACK_TEST_CONTENT(errno, expected); }

// Checks that we don't crash if zxdb doesn't provide a stack.
BAD_STACK_TEST(BadStack, ZX_OK, "");

}  // namespace fidlcat
