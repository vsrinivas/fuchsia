// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_arm64_helpers.h"

#include <zircon/hw/debug/arm64.h>
#include <zircon/status.h>

#include <optional>

#include <gtest/gtest.h>

#include "src/developer/debug/ipc/register_test_support.h"
#include "src/developer/debug/shared/logging/file_line_function.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/arraysize.h"

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
                           zx_thread_state_debug_regs_t* debug_regs, uint64_t address,
                           zx_status_t expected_result) {
  zx_status_t result = SetupHWBreakpoint(address, debug_regs);
  ASSERT_EQ(result, expected_result)
      << "[" << file_line.ToString() << "] "
      << "Got: " << debug_ipc::ZxStatusToString(result)
      << ", expected: " << debug_ipc::ZxStatusToString(expected_result);
}

void RemoveHWBreakpointTest(debug_ipc::FileLineFunction file_line,
                            zx_thread_state_debug_regs_t* debug_regs, uint64_t address,
                            zx_status_t expected_result) {
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
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress5, ZX_ERR_NO_RESOURCES);
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
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress5, ZX_ERR_NO_RESOURCES);

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
  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_ERR_OUT_OF_RANGE);
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
  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, 0xaaaaaaa, ZX_ERR_OUT_OF_RANGE);
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

  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_ERR_NO_RESOURCES);
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
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_ERR_NO_RESOURCES);
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

TEST(ArmHelpers, WriteGeneralRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kARMv8_x0, 8));
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kARMv8_x3, 8));
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kARMv8_lr, 8));
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kARMv8_pc, 8));

  zx_thread_state_general_regs_t out = {};
  zx_status_t res = WriteGeneralRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.r[0], 0x0102030405060708u);
  EXPECT_EQ(out.r[1], 0u);
  EXPECT_EQ(out.r[2], 0u);
  EXPECT_EQ(out.r[3], 0x0102030405060708u);
  EXPECT_EQ(out.r[4], 0u);
  EXPECT_EQ(out.r[29], 0u);
  EXPECT_EQ(out.lr, 0x0102030405060708u);
  EXPECT_EQ(out.pc, 0x0102030405060708u);

  regs.clear();
  regs.push_back(CreateUint64Register(debug_ipc::RegisterID::kARMv8_x0, 0xaabb));
  regs.push_back(CreateUint64Register(debug_ipc::RegisterID::kARMv8_x15, 0xdead));
  regs.push_back(CreateUint64Register(debug_ipc::RegisterID::kARMv8_pc, 0xbeef));

  res = WriteGeneralRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.r[0], 0xaabbu);
  EXPECT_EQ(out.r[1], 0u);
  EXPECT_EQ(out.r[15], 0xdeadu);
  EXPECT_EQ(out.r[29], 0u);
  EXPECT_EQ(out.lr, 0x0102030405060708u);
  EXPECT_EQ(out.pc, 0xbeefu);
}

TEST(ArmHelpers, InvalidWriteGeneralRegs) {
  zx_thread_state_general_regs_t out;
  std::vector<debug_ipc::Register> regs;

  // Invalid length.
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kARMv8_v0, 4));
  EXPECT_EQ(WriteGeneralRegisters(regs, &out), ZX_ERR_INVALID_ARGS);

  // Invalid (non-canonical) register.
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kARMv8_w3, 8));
  EXPECT_EQ(WriteGeneralRegisters(regs, &out), ZX_ERR_INVALID_ARGS);
}

TEST(ArmHelpers, WriteVectorRegs) {
  std::vector<debug_ipc::Register> regs;

  std::vector<uint8_t> v0_value;
  v0_value.resize(16);
  v0_value.front() = 0x42;
  v0_value.back() = 0x12;
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_v0, v0_value);

  std::vector<uint8_t> v31_value = v0_value;
  v31_value.front()++;
  v31_value.back()++;
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_v31, v31_value);

  regs.emplace_back(debug_ipc::RegisterID::kARMv8_fpcr, std::vector<uint8_t>{5, 6, 7, 8});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_fpsr, std::vector<uint8_t>{9, 0, 1, 2});

  zx_thread_state_vector_regs_t out = {};
  zx_status_t res = WriteVectorRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.v[0].low, 0x0000000000000042u);
  EXPECT_EQ(out.v[0].high, 0x1200000000000000u);
  EXPECT_EQ(out.v[31].low, 0x0000000000000043u);
  EXPECT_EQ(out.v[31].high, 0x1300000000000000u);

  EXPECT_EQ(out.fpcr, 0x08070605u);
  EXPECT_EQ(out.fpsr, 0x02010009u);
}

TEST(ArmHelpers, WriteDebugRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbcr0_el1, std::vector<uint8_t>{1, 2, 3, 4});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbcr1_el1, std::vector<uint8_t>{2, 3, 4, 5});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbcr15_el1, std::vector<uint8_t>{3, 4, 5, 6});

  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbvr0_el1,
                    std::vector<uint8_t>{4, 5, 6, 7, 8, 9, 0, 1});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbvr1_el1,
                    std::vector<uint8_t>{5, 6, 7, 8, 9, 0, 1, 2});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbvr15_el1,
                    std::vector<uint8_t>{6, 7, 8, 9, 0, 1, 2, 3});

  // TODO(bug 40992) Add ARM64 hardware watchpoint registers here.

  zx_thread_state_debug_regs_t out = {};
  zx_status_t res = WriteDebugRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.hw_bps[0].dbgbcr, 0x04030201u);
  EXPECT_EQ(out.hw_bps[1].dbgbcr, 0x05040302u);
  EXPECT_EQ(out.hw_bps[15].dbgbcr, 0x06050403u);
  EXPECT_EQ(out.hw_bps[0].dbgbvr, 0x0100090807060504u);
  EXPECT_EQ(out.hw_bps[1].dbgbvr, 0x0201000908070605u);
  EXPECT_EQ(out.hw_bps[15].dbgbvr, 0x0302010009080706u);
}

// Watchpoint Tests --------------------------------------------------------------------------------

constexpr uint32_t kWatchpointCount = 4;

bool ResultVerification(zx_thread_state_debug_regs_t* regs, uint64_t address, uint64_t size,
                        WatchpointInstallationResult expected) {
  WatchpointInstallationResult result =
      SetupWatchpoint(regs, {address, address + size}, kWatchpointCount);
  if (result.status != expected.status) {
    ADD_FAILURE() << "Status failed. Expected: " << zx_status_get_string(expected.status)
                  << ", got: " << zx_status_get_string(result.status);
    return false;
  }

  if (result.installed_range != expected.installed_range) {
    ADD_FAILURE() << "Range failed. Expected: " << expected.installed_range.ToString()
                  << ", got: " << result.installed_range.ToString();
    return false;
  }

  if (result.slot != expected.slot) {
    ADD_FAILURE() << "Slot failed. Expected: " << expected.slot << ", got: " << result.slot;
    return false;
  }

  return true;
}

bool Check(zx_thread_state_debug_regs_t* regs, uint64_t address, uint64_t size,
           WatchpointInstallationResult expected, uint32_t expected_bas = 0) {
  if (!ResultVerification(regs, address, size, expected))
    return false;

  // If no installation was made, we don't compare against BAS.
  if (expected.slot == -1)
    return true;

  uint32_t bas = ARM64_DBGWCR_BAS_GET(regs->hw_wps[expected.slot].dbgwcr);
  if (bas != expected_bas) {
    ADD_FAILURE() << "BAS check failed. Expected: 0x" << std::hex << expected_bas << ", got: 0x"
                  << bas;
    return false;
  }

  return true;
}

bool ResetCheck(zx_thread_state_debug_regs_t* regs, uint64_t address, uint64_t size,
                WatchpointInstallationResult expected, uint32_t expected_bas = 0) {
  // Restart the registers.
  *regs = {};
  return Check(regs, address, size, expected, expected_bas);
}

bool CheckAddresses(const zx_thread_state_debug_regs_t& regs, std::vector<uint64_t> addresses) {
  for (uint32_t i = 0; i < addresses.size(); i++) {
    if (regs.hw_wps[i].dbgwvr != addresses[i]) {
      ADD_FAILURE() << "Reg " << i << " mismatch. Expected: 0x" << std::hex << addresses[i]
                    << ", got: " << regs.hw_wps[i].dbgwvr;
      return false;
    }
  }

  return true;
}

uint32_t CountBASBits(uint32_t bas) {
  switch (bas) {
    case 0b0000000:
      return 0;
    case 0b00000001:
    case 0b00000010:
    case 0b00000100:
    case 0b00001000:
    case 0b00010000:
    case 0b00100000:
    case 0b01000000:
    case 0b10000000:
      return 1;
    case 0b00000011:
    case 0b00001100:
    case 0b00110000:
    case 0b11000000:
      return 2;
    case 0b00001111:
    case 0b11110000:
      return 4;
    case 0b11111111:
      return 8;
    default:
      FXL_NOTREACHED() << "Invalid bas: 0x" << std::hex << bas;
      return 0;
  }
}

bool CheckLengths(const zx_thread_state_debug_regs_t& regs, std::vector<uint32_t> lengths) {
  for (uint32_t i = 0; i < lengths.size(); i++) {
    uint32_t bas = ARM64_DBGWCR_BAS_GET(regs.hw_wps[i].dbgwcr);
    uint32_t length = CountBASBits(bas);

    if (length != lengths[i]) {
      ADD_FAILURE() << "Reg " << i << " wrong length. Expected " << lengths[i]
                    << ", got: " << length;
      return false;
    }
  }

  return true;
}

bool CheckEnabled(const zx_thread_state_debug_regs& regs, std::vector<uint32_t> enabled) {
  for (uint32_t i = 0; i < enabled.size(); i++) {
    uint32_t e = ARM64_DBGWCR_E_GET(regs.hw_wps[i].dbgwcr);
    if (e != enabled[i]) {
      ADD_FAILURE() << "Reg " << i << " wrong enable. Expected: " << enabled[i] << ", got: " << e;
      return false;
    }
  }

  return true;
}

/* constexpr uint32_t kRead = 0b01; */
constexpr uint32_t kWrite = 0b10;
/* constexpr uint32_t kReadWrite = 0b11; */

bool CheckTypes(const zx_thread_state_debug_regs& regs, std::vector<uint32_t> types) {
  for (uint32_t i = 0; i < types.size(); i++) {
    uint32_t type = ARM64_DBGWCR_LSC_GET(regs.hw_wps[i].dbgwcr);
    if (type != types[i]) {
      ADD_FAILURE() << "Reg " << i << " wrong type. Expected: " << types[i] << ", got: " << type;
      return false;
    }
  }

  return true;
}

// Tests -------------------------------------------------------------------------------------------

TEST(ArmHelpers_SetupWatchpoint, SetupMany) {
  zx_thread_state_debug_regs regs = {};

  // Always aligned address.
  constexpr uint64_t kAddress1 = 0x10000;
  constexpr uint64_t kAddress2 = 0x20000;
  constexpr uint64_t kAddress3 = 0x30000;
  constexpr uint64_t kAddress4 = 0x40000;
  constexpr uint64_t kAddress5 = 0x50000;

  ASSERT_TRUE(Check(&regs, kAddress1, 1, CreateResult(ZX_OK, {kAddress1, kAddress1 + 1}, 0), 0x1));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(Check(&regs, kAddress1, 1, CreateResult(ZX_ERR_ALREADY_BOUND)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(Check(&regs, kAddress2, 2, CreateResult(ZX_OK, {kAddress2, kAddress2 + 2}, 1), 0x3));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, 0}));

  ASSERT_TRUE(Check(&regs, kAddress3, 4, CreateResult(ZX_OK, {kAddress3, kAddress3 + 4}, 2), 0xf));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, 0}));

  ASSERT_TRUE(Check(&regs, kAddress4, 8, CreateResult(ZX_OK, {kAddress4, kAddress4 + 8}, 3), 0xff));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  ASSERT_TRUE(Check(&regs, kAddress5, 8, CreateResult(ZX_ERR_NO_RESOURCES)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));
}

TEST(ArmHelpers_SetupWatchpoint, Ranges) {
  zx_thread_state_debug_regs_t regs = {};

  // 1-byte alignment.
  ASSERT_TRUE(ResetCheck(&regs, 0x1000, 1, CreateResult(ZX_OK, {0x1000, 0x1001}, 0), 0b00000001));
  ASSERT_TRUE(ResetCheck(&regs, 0x1001, 1, CreateResult(ZX_OK, {0x1001, 0x1002}, 0), 0b00000010));
  ASSERT_TRUE(ResetCheck(&regs, 0x1002, 1, CreateResult(ZX_OK, {0x1002, 0x1003}, 0), 0b00000100));
  ASSERT_TRUE(ResetCheck(&regs, 0x1003, 1, CreateResult(ZX_OK, {0x1003, 0x1004}, 0), 0b00001000));
  ASSERT_TRUE(ResetCheck(&regs, 0x1004, 1, CreateResult(ZX_OK, {0x1004, 0x1005}, 0), 0b00000001));
  ASSERT_TRUE(ResetCheck(&regs, 0x1005, 1, CreateResult(ZX_OK, {0x1005, 0x1006}, 0), 0b00000010));
  ASSERT_TRUE(ResetCheck(&regs, 0x1006, 1, CreateResult(ZX_OK, {0x1006, 0x1007}, 0), 0b00000100));
  ASSERT_TRUE(ResetCheck(&regs, 0x1007, 1, CreateResult(ZX_OK, {0x1007, 0x1008}, 0), 0b00001000));
  ASSERT_TRUE(ResetCheck(&regs, 0x1008, 1, CreateResult(ZX_OK, {0x1008, 0x1009}, 0), 0b00000001));
  ASSERT_TRUE(ResetCheck(&regs, 0x1009, 1, CreateResult(ZX_OK, {0x1009, 0x100a}, 0), 0b00000010));
  ASSERT_TRUE(ResetCheck(&regs, 0x100a, 1, CreateResult(ZX_OK, {0x100a, 0x100b}, 0), 0b00000100));
  ASSERT_TRUE(ResetCheck(&regs, 0x100b, 1, CreateResult(ZX_OK, {0x100b, 0x100c}, 0), 0b00001000));
  ASSERT_TRUE(ResetCheck(&regs, 0x100c, 1, CreateResult(ZX_OK, {0x100c, 0x100d}, 0), 0b00000001));
  ASSERT_TRUE(ResetCheck(&regs, 0x100d, 1, CreateResult(ZX_OK, {0x100d, 0x100e}, 0), 0b00000010));
  ASSERT_TRUE(ResetCheck(&regs, 0x100e, 1, CreateResult(ZX_OK, {0x100e, 0x100f}, 0), 0b00000100));
  ASSERT_TRUE(ResetCheck(&regs, 0x100f, 1, CreateResult(ZX_OK, {0x100f, 0x1010}, 0), 0b00001000));
  ASSERT_TRUE(ResetCheck(&regs, 0x1010, 1, CreateResult(ZX_OK, {0x1010, 0x1011}, 0), 0b00000001));

  // 2-byte alignment.
  ASSERT_TRUE(ResetCheck(&regs, 0x1000, 2, CreateResult(ZX_OK, {0x1000, 0x1002}, 0), 0b00000011));
  ASSERT_TRUE(ResetCheck(&regs, 0x1001, 2, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1002, 2, CreateResult(ZX_OK, {0x1002, 0x1004}, 0), 0b00001100));
  ASSERT_TRUE(ResetCheck(&regs, 0x1003, 2, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1004, 2, CreateResult(ZX_OK, {0x1004, 0x1006}, 0), 0b00000011));
  ASSERT_TRUE(ResetCheck(&regs, 0x1005, 2, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1006, 2, CreateResult(ZX_OK, {0x1006, 0x1008}, 0), 0b00001100));
  ASSERT_TRUE(ResetCheck(&regs, 0x1007, 2, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1008, 2, CreateResult(ZX_OK, {0x1008, 0x100a}, 0), 0b00000011));
  ASSERT_TRUE(ResetCheck(&regs, 0x1009, 2, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100a, 2, CreateResult(ZX_OK, {0x100a, 0x100c}, 0), 0b00001100));
  ASSERT_TRUE(ResetCheck(&regs, 0x100b, 2, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x100c, 2, CreateResult(ZX_OK, {0x100c, 0x100e}, 0), 0b00000011));
  ASSERT_TRUE(ResetCheck(&regs, 0x100d, 2, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100e, 2, CreateResult(ZX_OK, {0x100e, 0x1010}, 0), 0b00001100));
  ASSERT_TRUE(ResetCheck(&regs, 0x100f, 2, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1010, 2, CreateResult(ZX_OK, {0x1010, 0x1012}, 0), 0b00000011));

  // 3-byte alignment.
  ASSERT_TRUE(ResetCheck(&regs, 0x1000, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1001, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1002, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1003, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1004, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1005, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1006, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1007, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1008, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1009, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100a, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100b, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  // 4 byte range.
  ASSERT_TRUE(ResetCheck(&regs, 0x1000, 4, CreateResult(ZX_OK, {0x1000, 0x1004}, 0), 0x0f));
  ASSERT_TRUE(ResetCheck(&regs, 0x1001, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1002, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1003, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1004, 4, CreateResult(ZX_OK, {0x1004, 0x1008}, 0), 0x0f));
  ASSERT_TRUE(ResetCheck(&regs, 0x1005, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1006, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1007, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1008, 4, CreateResult(ZX_OK, {0x1008, 0x100c}, 0), 0x0f));
  ASSERT_TRUE(ResetCheck(&regs, 0x1009, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100a, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100b, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x100c, 4, CreateResult(ZX_OK, {0x100c, 0x1010}, 0), 0x0f));

  // 5 byte range.
  ASSERT_TRUE(ResetCheck(&regs, 0x1000, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1001, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1002, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1003, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1004, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1005, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1006, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1007, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1008, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1009, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100a, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100b, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100c, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100d, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100e, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100f, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  // 6 byte range.
  ASSERT_TRUE(ResetCheck(&regs, 0x1000, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1001, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1002, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1003, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1004, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1005, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1006, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1007, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1008, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1009, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100a, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100b, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100c, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100d, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100e, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100f, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  // 7 byte range.
  ASSERT_TRUE(ResetCheck(&regs, 0x1000, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1001, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1002, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1003, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1004, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1005, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1006, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1007, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1008, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1009, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100a, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100b, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100c, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100d, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100e, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100f, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  // 8 byte range.
  ASSERT_TRUE(ResetCheck(&regs, 0x1000, 8, CreateResult(ZX_OK, {0x1000, 0x1008}, 0), 0xff));
  ASSERT_TRUE(ResetCheck(&regs, 0x1001, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1002, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1003, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1004, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1005, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1006, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x1007, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(ResetCheck(&regs, 0x1008, 8, CreateResult(ZX_OK, {0x1008, 0x1010}, 0), 0xff));
  ASSERT_TRUE(ResetCheck(&regs, 0x1009, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100a, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100b, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100c, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100d, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100e, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(ResetCheck(&regs, 0x100f, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
}

TEST(ArmHelpers_SettingWatchpoints, RangeIsDifferentWatchpoint) {
  zx_thread_state_debug_regs_t regs = {};

  ASSERT_TRUE(Check(&regs, 0x100, 1, CreateResult(ZX_OK, {0x100, 0x100 + 1}, 0), 0b00000001));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 0, 0, 0}));

  ASSERT_TRUE(Check(&regs, 0x100, 1, CreateResult(ZX_ERR_ALREADY_BOUND)));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 0, 0, 0}));

  ASSERT_TRUE(Check(&regs, 0x100, 2, CreateResult(ZX_OK, {0x100, 0x100 + 2}, 1), 0b00000011));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 0, 0}));

  ASSERT_TRUE(Check(&regs, 0x100, 2, CreateResult(ZX_ERR_ALREADY_BOUND)));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 0, 0}));

  ASSERT_TRUE(Check(&regs, 0x100, 4, CreateResult(ZX_OK, {0x100, 0x100 + 4}, 2), 0b00001111));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0x100, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 4, 0}));

  ASSERT_TRUE(Check(&regs, 0x100, 4, CreateResult(ZX_ERR_ALREADY_BOUND)));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0x100, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 4, 0}));

  ASSERT_TRUE(Check(&regs, 0x100, 8, CreateResult(ZX_OK, {0x100, 0x100 + 8}, 3), 0b11111111));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0x100, 0x100}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
}

}  // namespace
}  // namespace arch
}  // namespace debug_agent
