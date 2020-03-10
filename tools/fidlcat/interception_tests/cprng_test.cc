// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_cprng_draw tests.

std::unique_ptr<SystemCallTest> ZxCprngDraw(int64_t result, std::string_view result_name,
                                            void* buffer, size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_cprng_draw", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define CPRNG_DRAW_DISPLAY_TEST_CONTENT(result, expected) \
  std::vector<uint8_t> buffer;                            \
  for (int i = 0; i < 20; ++i) {                          \
    buffer.emplace_back(i);                               \
  }                                                       \
  PerformDisplayTest("$plt(zx_cprng_draw)",               \
                     ZxCprngDraw(result, #result, buffer.data(), buffer.size()), expected);

#define CPRNG_DRAW_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { CPRNG_DRAW_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { CPRNG_DRAW_DISPLAY_TEST_CONTENT(errno, expected); }

CPRNG_DRAW_DISPLAY_TEST(
    ZxCprngDraw, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_cprng_draw()\n"
    "  -> \n"
    "      buffer:\x1B[32muint8\x1B[0m: "
    "\x1B[34m00\x1B[0m, \x1B[34m01\x1B[0m, \x1B[34m02\x1B[0m, \x1B[34m03\x1B[0m, "
    "\x1B[34m04\x1B[0m, \x1B[34m05\x1B[0m, \x1B[34m06\x1B[0m, \x1B[34m07\x1B[0m, "
    "\x1B[34m08\x1B[0m, \x1B[34m09\x1B[0m, \x1B[34m0a\x1B[0m, \x1B[34m0b\x1B[0m, "
    "\x1B[34m0c\x1B[0m, \x1B[34m0d\x1B[0m, \x1B[34m0e\x1B[0m, \x1B[34m0f\x1B[0m, "
    "\x1B[34m10\x1B[0m, \x1B[34m11\x1B[0m, \x1B[34m12\x1B[0m, \x1B[34m13\x1B[0m\n");

// zx_cprng_add_entropy tests.

std::unique_ptr<SystemCallTest> ZxCprngAddEntropy(int64_t result, std::string_view result_name,
                                                  const void* buffer, size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_cprng_add_entropy", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define CPRNG_ADD_ENTROPY_DISPLAY_TEST_CONTENT(result, expected) \
  std::vector<uint8_t> buffer;                                   \
  for (int i = 0; i < 20; ++i) {                                 \
    buffer.emplace_back(i);                                      \
  }                                                              \
  PerformDisplayTest("$plt(zx_cprng_add_entropy)",               \
                     ZxCprngAddEntropy(result, #result, buffer.data(), buffer.size()), expected);

#define CPRNG_ADD_ENTROPY_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                 \
    CPRNG_ADD_ENTROPY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                           \
  TEST_F(InterceptionWorkflowTestArm, name) {                 \
    CPRNG_ADD_ENTROPY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

CPRNG_ADD_ENTROPY_DISPLAY_TEST(
    ZxCprngAddEntropy, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_cprng_add_entropy()\n"
    "    buffer:\x1B[32muint8\x1B[0m: "
    "\x1B[34m00\x1B[0m, \x1B[34m01\x1B[0m, \x1B[34m02\x1B[0m, \x1B[34m03\x1B[0m, "
    "\x1B[34m04\x1B[0m, \x1B[34m05\x1B[0m, \x1B[34m06\x1B[0m, \x1B[34m07\x1B[0m, "
    "\x1B[34m08\x1B[0m, \x1B[34m09\x1B[0m, \x1B[34m0a\x1B[0m, \x1B[34m0b\x1B[0m, "
    "\x1B[34m0c\x1B[0m, \x1B[34m0d\x1B[0m, \x1B[34m0e\x1B[0m, \x1B[34m0f\x1B[0m, "
    "\x1B[34m10\x1B[0m, \x1B[34m11\x1B[0m, \x1B[34m12\x1B[0m, \x1B[34m13\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
