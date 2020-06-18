// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/exception.h>
#include <zxtest/zxtest.h>

namespace {

TEST(ExceptionGetString, AllOfEm) {
  for (uint32_t i = 0; i < UINT16_MAX; i++) {
    switch (i) {
      case ZX_EXCP_GENERAL:
        EXPECT_STR_EQ(zx_exception_get_string(i), "ZX_EXCP_GENERAL");
        continue;
      case ZX_EXCP_FATAL_PAGE_FAULT:
        EXPECT_STR_EQ(zx_exception_get_string(i), "ZX_EXCP_FATAL_PAGE_FAULT");
        continue;
      case ZX_EXCP_UNDEFINED_INSTRUCTION:
        EXPECT_STR_EQ(zx_exception_get_string(i), "ZX_EXCP_UNDEFINED_INSTRUCTION");
        continue;
      case ZX_EXCP_SW_BREAKPOINT:
        EXPECT_STR_EQ(zx_exception_get_string(i), "ZX_EXCP_SW_BREAKPOINT");
        continue;
      case ZX_EXCP_HW_BREAKPOINT:
        EXPECT_STR_EQ(zx_exception_get_string(i), "ZX_EXCP_HW_BREAKPOINT");
        continue;
      case ZX_EXCP_UNALIGNED_ACCESS:
        EXPECT_STR_EQ(zx_exception_get_string(i), "ZX_EXCP_UNALIGNED_ACCESS");
        continue;
      case ZX_EXCP_THREAD_STARTING:
        EXPECT_STR_EQ(zx_exception_get_string(i), "ZX_EXCP_THREAD_STARTING");
        continue;
      case ZX_EXCP_THREAD_EXITING:
        EXPECT_STR_EQ(zx_exception_get_string(i), "ZX_EXCP_THREAD_EXITING");
        continue;
      case ZX_EXCP_POLICY_ERROR:
        EXPECT_STR_EQ(zx_exception_get_string(i), "ZX_EXCP_POLICY_ERROR");
        continue;
      case ZX_EXCP_PROCESS_STARTING:
        EXPECT_STR_EQ(zx_exception_get_string(i), "ZX_EXCP_PROCESS_STARTING");
        continue;
      default:
        EXPECT_STR_EQ(zx_exception_get_string(i), "(UNKNOWN)");
        continue;
    }
  }
}

}  // namespace
