// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_cache_flush tests.

std::unique_ptr<SystemCallTest> ZxCacheFlush(int64_t result, std::string_view result_name,
                                             const void* addr, size_t size, uint32_t options) {
  auto value = std::make_unique<SystemCallTest>("zx_cache_flush", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(addr));
  value->AddInput(size);
  value->AddInput(options);
  return value;
}

#define CACHE_FLUSH_DISPLAY_TEST_CONTENT(result, expected)                                 \
  const void* addr = reinterpret_cast<const void*>(0x1234567890);                          \
  size_t size = 4096;                                                                      \
  PerformDisplayTest("$plt(zx_cache_flush)", ZxCacheFlush(result, #result, addr, size, 0), \
                     expected);

#define CACHE_FLUSH_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { CACHE_FLUSH_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { CACHE_FLUSH_DISPLAY_TEST_CONTENT(errno, expected); }

CACHE_FLUSH_DISPLAY_TEST(ZxCacheFlush, ZX_OK,
                         "\n"
                         "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_cache_flush("
                         "addr:\x1B[32mzx_vaddr_t\x1B[0m: \x1B[34m0000001234567890\x1B[0m, "
                         "size:\x1B[32msize_t\x1B[0m: \x1B[34m4096\x1B[0m, "
                         "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                         "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
