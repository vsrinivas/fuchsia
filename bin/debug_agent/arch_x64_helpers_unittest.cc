// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/arch_x64_helpers.h"

#include <gtest/gtest.h>

namespace debug_agent {
namespace arch {

namespace {

zx_thread_state_debug_regs_t GetDefaultRegs() {
  zx_thread_state_debug_regs_t debug_regs = {};
  debug_regs.dr6 = kDR6Mask;
  debug_regs.dr7 = kDR7Mask;

  return debug_regs;
}

constexpr uint64_t kAddress1 = 0x0123;
constexpr uint64_t kAddress2 = 0x4567;
constexpr uint64_t kAddress3 = 0x89ab;
constexpr uint64_t kAddress4 = 0xcdef;
constexpr uint64_t kAddress5 = 0xdeadbeef;

}  // namespace

TEST(x64Helpers, SettingBreakpoints) {
  auto debug_regs = GetDefaultRegs();

  ASSERT_EQ(SetupDebugBreakpoint(kAddress1, &debug_regs), ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], 0u);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], 0u);
  EXPECT_EQ(debug_regs.dr6, kDR6Mask);
  EXPECT_EQ(debug_regs.dr7, kDR7Mask | kDR7L0 | 0 | 0 | 0);

  // Continuing adding should append.
  ASSERT_EQ(SetupDebugBreakpoint(kAddress2, &debug_regs), ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], 0u);
  EXPECT_EQ(debug_regs.dr6, kDR6Mask);
  EXPECT_EQ(debug_regs.dr7, kDR7Mask | kDR7L0 | kDR7L1 | 0 | 0);

  ASSERT_EQ(SetupDebugBreakpoint(kAddress3, &debug_regs), ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], kAddress3);
  EXPECT_EQ(debug_regs.dr[3], 0u);
  EXPECT_EQ(debug_regs.dr6, kDR6Mask);
  EXPECT_EQ(debug_regs.dr7, kDR7Mask | kDR7L0 | kDR7L1 | kDR7L2 | 0);

  ASSERT_EQ(SetupDebugBreakpoint(kAddress4, &debug_regs), ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], kAddress3);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, kDR6Mask);
  EXPECT_EQ(debug_regs.dr7, kDR7Mask | kDR7L0 | kDR7L1 | kDR7L2 | kDR7L3);

  // TODO(donosoc): Test adding the same address twice.

  // No more registers left.
  ASSERT_EQ(SetupDebugBreakpoint(kAddress5, &debug_regs), ZX_ERR_NO_RESOURCES);
}

TEST(x64Helpers, Removing) {
  auto debug_regs = GetDefaultRegs();

  // Previous state verifies the state of this calls.
  ASSERT_EQ(SetupDebugBreakpoint(kAddress1, &debug_regs), ZX_OK);
  ASSERT_EQ(SetupDebugBreakpoint(kAddress2, &debug_regs), ZX_OK);
  ASSERT_EQ(SetupDebugBreakpoint(kAddress3, &debug_regs), ZX_OK);
  ASSERT_EQ(SetupDebugBreakpoint(kAddress4, &debug_regs), ZX_OK);

  ASSERT_EQ(RemoveDebugBreakpoint(kAddress3, &debug_regs), ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, kDR6Mask);
  EXPECT_EQ(debug_regs.dr7, kDR7Mask | kDR7L0 | kDR7L1 | 0 | kDR7L3);

  // Removing same breakpoint should not work.
  ASSERT_EQ(RemoveDebugBreakpoint(kAddress3, &debug_regs), ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, kDR6Mask);
  EXPECT_EQ(debug_regs.dr7, kDR7Mask | kDR7L0 | kDR7L1 | 0 | kDR7L3);

  // Removing an unknown register should warn and change nothing.
  ASSERT_EQ(RemoveDebugBreakpoint(0xaaaaaaa, &debug_regs), ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, kDR6Mask);
  EXPECT_EQ(debug_regs.dr7, kDR7Mask | kDR7L0 | kDR7L1 | 0 | kDR7L3);

  ASSERT_EQ(RemoveDebugBreakpoint(kAddress1, &debug_regs), ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], 0u);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, kDR6Mask);
  EXPECT_EQ(debug_regs.dr7, kDR7Mask | 0 | kDR7L1 | 0 | kDR7L3);

  // Adding again should work.
  ASSERT_EQ(SetupDebugBreakpoint(kAddress5, &debug_regs), ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress5);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, kDR6Mask);
  EXPECT_EQ(debug_regs.dr7, kDR7Mask | kDR7L0 | kDR7L1 | 0 | kDR7L3);
}

}  // namespace arch
}  // namespace debug_agent
