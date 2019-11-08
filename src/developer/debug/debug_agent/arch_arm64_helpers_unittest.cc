// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_arm64_helpers.h"

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

TEST(armHelpers, WriteGeneralRegs) {
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

TEST(armHelpers, InvalidWriteGeneralRegs) {
  zx_thread_state_general_regs_t out;
  std::vector<debug_ipc::Register> regs;

  // Invalid length.
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kARMv8_v0, 4));
  EXPECT_EQ(WriteGeneralRegisters(regs, &out), ZX_ERR_INVALID_ARGS);

  // Invalid (non-canonical) register.
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kARMv8_w3, 8));
  EXPECT_EQ(WriteGeneralRegisters(regs, &out), ZX_ERR_INVALID_ARGS);
}

TEST(armHelpers, WriteVectorRegs) {
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

TEST(armHelpers, WriteDebugRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbcr0_el1,
                    std::vector<uint8_t>{1, 2, 3, 4});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbcr1_el1,
                    std::vector<uint8_t>{2, 3, 4, 5});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbcr15_el1,
                    std::vector<uint8_t>{3, 4, 5, 6});

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

}  // namespace arch
}  // namespace debug_agent
