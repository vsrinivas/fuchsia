// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/arch_arm64_helpers.h"

#include <gtest/gtest.h>

#include <optional>

#include "lib/fxl/arraysize.h"
#include "src/developer/debug/ipc/debug/file_line_function.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {
namespace arch {

namespace {

constexpr uint64_t kDbgbvrE = 1u;

zx_thread_state_debug_regs_t GetDefaultRegs() {
  zx_thread_state_debug_regs_t debug_regs = {};
  debug_regs.hw_bps_count = 4;

  return debug_regs;
}

void SetupHWBreakpointTest(debug_ipc::FileLineFunction file_line,
                           zx_thread_state_debug_regs_t* debug_regs,
                           uint64_t address, zx_status_t expected_result) {
  zx_status_t result = SetupHWBreakpoint(address, debug_regs);
  ASSERT_EQ(result, expected_result)
      << "[" << file_line.ToString() << "] "
      << "Got: " << debug_ipc::ZxStatusToString(result)
      << ", expected: " << debug_ipc::ZxStatusToString(expected_result);
}

void RemoveHWBreakpointTest(debug_ipc::FileLineFunction file_line,
                            zx_thread_state_debug_regs_t* debug_regs,
                            uint64_t address, zx_status_t expected_result) {
  zx_status_t result = RemoveHWBreakpoint(address, debug_regs);
  ASSERT_EQ(result, expected_result)
      << "[" << file_line.ToString() << "] "
      << "Got: " << debug_ipc::ZxStatusToString(result)
      << ", expected: " << debug_ipc::ZxStatusToString(expected_result);
}

constexpr uint64_t kAddress1 = 0x0123;
constexpr uint64_t kAddress2 = 0x4567;
constexpr uint64_t kAddress3 = 0x89ab;
constexpr uint64_t kAddress4 = 0xcdef;
constexpr uint64_t kAddress5 = 0xdeadbeef;

}  // namespace

TEST(arm64Helpers, SettingBreakpoints) {
  auto debug_regs = GetDefaultRegs();

  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress1, ZX_OK);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress1);
  for (size_t i = 1; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Adding the same breakpoint should detect that the same already exists.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress1, ZX_OK);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress1);
  for (size_t i = 1; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Continuing adding should append.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress2, ZX_OK);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  for (size_t i = 2; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_OK);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, kAddress3);
  for (size_t i = 3; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress4, ZX_OK);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, kAddress3);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  // No more registers left should not change anything.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress5,
                        ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, kAddress3);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }
}

TEST(arm64Helpers, Removing) {
  auto debug_regs = GetDefaultRegs();

  // Previous state verifies the state of this calls.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress1, ZX_OK);
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress2, ZX_OK);
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_OK);
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress4, ZX_OK);
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress5,
                        ZX_ERR_NO_RESOURCES);

  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_OK);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, 0u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Removing same breakpoint should not work.
  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3,
                         ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, 0u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Removing an unknown address should warn and change nothing.
  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, 0xaaaaaaa,
                         ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, 0u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress1, ZX_OK);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, 0u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, 0u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Adding again should work.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress5, ZX_OK);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress5);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, 0u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress1, ZX_OK);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress5);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Already exists should not change anything.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress5, ZX_OK);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress5);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3,
                        ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress5);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }

  // No more registers.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3,
                        ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[0].dbgbvr, kAddress5);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[2].dbgbvr, kAddress1);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(debug_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < arraysize(debug_regs.hw_bps); i++) {
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(debug_regs.hw_bps[i].dbgbvr, 0u);
  }
}

}  // namespace arch
}  // namespace debug_agent
