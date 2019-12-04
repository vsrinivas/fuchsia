// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_x64_helpers.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/test_utils.h"
#include "src/developer/debug/ipc/register_test_support.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/developer/debug/shared/logging/file_line_function.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/logging.h"

using namespace debug_ipc;

namespace debug_agent {
namespace arch {

namespace {

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

uint64_t GetHWBreakpointDR7Mask(size_t index) {
  FXL_DCHECK(index < 4);
  // Mask is: L = 1, RW = 00, LEN = 0
  static uint64_t dr_masks[4] = {
      X86_FLAG_MASK(DR7L0),
      X86_FLAG_MASK(DR7L1),
      X86_FLAG_MASK(DR7L2),
      X86_FLAG_MASK(DR7L3),
  };
  return dr_masks[index];
}

// Merges into |val| the flag values for active hw breakpoints within |indices|.
uint64_t JoinDR7HWBreakpointMask(uint64_t val, std::initializer_list<size_t> indices = {}) {
  for (size_t index : indices) {
    FXL_DCHECK(index < 4);
    val |= GetHWBreakpointDR7Mask(index);
  }

  return val;
}

}  // namespace

// Register writing --------------------------------------------------------------------------------

TEST(x64Helpers, WriteGeneralRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kX64_rax, 8));
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kX64_rbx, 8));
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kX64_r14, 8));
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kX64_rflags, 8));

  zx_thread_state_general_regs_t out = {};
  zx_status_t res = WriteGeneralRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.rax, 0x0102030405060708u);
  EXPECT_EQ(out.rbx, 0x0102030405060708u);
  EXPECT_EQ(out.rcx, 0u);
  EXPECT_EQ(out.rdx, 0u);
  EXPECT_EQ(out.rsi, 0u);
  EXPECT_EQ(out.rdi, 0u);
  EXPECT_EQ(out.rbp, 0u);
  EXPECT_EQ(out.rsp, 0u);
  EXPECT_EQ(out.r8, 0u);
  EXPECT_EQ(out.r9, 0u);
  EXPECT_EQ(out.r10, 0u);
  EXPECT_EQ(out.r11, 0u);
  EXPECT_EQ(out.r12, 0u);
  EXPECT_EQ(out.r13, 0u);
  EXPECT_EQ(out.r14, 0x0102030405060708u);
  EXPECT_EQ(out.r15, 0u);
  EXPECT_EQ(out.rip, 0u);
  EXPECT_EQ(out.rflags, 0x0102030405060708u);

  regs.clear();
  regs.push_back(CreateUint64Register(debug_ipc::RegisterID::kX64_rax, 0xaabb));
  regs.push_back(CreateUint64Register(debug_ipc::RegisterID::kX64_rdx, 0xdead));
  regs.push_back(CreateUint64Register(debug_ipc::RegisterID::kX64_r10, 0xbeef));

  res = WriteGeneralRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.rax, 0xaabbu);
  EXPECT_EQ(out.rbx, 0x0102030405060708u);
  EXPECT_EQ(out.rcx, 0u);
  EXPECT_EQ(out.rdx, 0xdeadu);
  EXPECT_EQ(out.rsi, 0u);
  EXPECT_EQ(out.rdi, 0u);
  EXPECT_EQ(out.rbp, 0u);
  EXPECT_EQ(out.rsp, 0u);
  EXPECT_EQ(out.r8, 0u);
  EXPECT_EQ(out.r9, 0u);
  EXPECT_EQ(out.r10, 0xbeefu);
  EXPECT_EQ(out.r11, 0u);
  EXPECT_EQ(out.r12, 0u);
  EXPECT_EQ(out.r13, 0u);
  EXPECT_EQ(out.r14, 0x0102030405060708u);
  EXPECT_EQ(out.r15, 0u);
  EXPECT_EQ(out.rip, 0u);
  EXPECT_EQ(out.rflags, 0x0102030405060708u);
}

TEST(x64Helpers, InvalidWriteGeneralRegs) {
  zx_thread_state_general_regs_t out;
  std::vector<debug_ipc::Register> regs;

  // Invalid length.
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kX64_rax, 4));
  EXPECT_EQ(WriteGeneralRegisters(regs, &out), ZX_ERR_INVALID_ARGS);

  // Invalid (non-canonical) register.
  regs.push_back(CreateRegisterWithData(debug_ipc::RegisterID::kX64_ymm2, 8));
  EXPECT_EQ(WriteGeneralRegisters(regs, &out), ZX_ERR_INVALID_ARGS);
}

TEST(x64Helpers, WriteFPRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.emplace_back(debug_ipc::RegisterID::kX64_fcw, std::vector<uint8_t>{1, 2});
  regs.emplace_back(debug_ipc::RegisterID::kX64_fsw, std::vector<uint8_t>{3, 4});
  regs.emplace_back(debug_ipc::RegisterID::kX64_ftw, std::vector<uint8_t>{6});
  regs.emplace_back(debug_ipc::RegisterID::kX64_fop, std::vector<uint8_t>{7, 8});
  regs.emplace_back(debug_ipc::RegisterID::kX64_fip, std::vector<uint8_t>{9, 0, 0, 0, 10, 0, 0, 0});
  regs.emplace_back(debug_ipc::RegisterID::kX64_fdp,
                    std::vector<uint8_t>{11, 0, 0, 0, 12, 0, 0, 0});

  zx_thread_state_fp_regs_t out = {};
  zx_status_t res = WriteFloatingPointRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.fcw, 0x0201u);
  EXPECT_EQ(out.fsw, 0x0403u);
  EXPECT_EQ(out.ftw, 0x06u);
  EXPECT_EQ(out.fop, 0x0807u);
  EXPECT_EQ(out.fip, 0x0000000a00000009u);
  EXPECT_EQ(out.fdp, 0x0000000c0000000bu);
}

TEST(x64Helpers, WriteVectorRegs) {
  std::vector<debug_ipc::Register> regs;

  std::vector<uint8_t> zmm0_value;
  zmm0_value.resize(64);
  zmm0_value.front() = 0x42;
  zmm0_value.back() = 0x12;
  regs.emplace_back(debug_ipc::RegisterID::kX64_zmm0, zmm0_value);

  std::vector<uint8_t> zmm31_value = zmm0_value;
  zmm31_value.front()++;
  zmm31_value.back()++;
  regs.emplace_back(debug_ipc::RegisterID::kX64_zmm31, zmm31_value);

  regs.emplace_back(debug_ipc::RegisterID::kX64_mxcsr, std::vector<uint8_t>{5, 6, 7, 8});

  zx_thread_state_vector_regs_t out = {};
  zx_status_t res = WriteVectorRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.zmm[0].v[0], 0x0000000000000042u);
  EXPECT_EQ(out.zmm[0].v[1], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[2], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[3], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[4], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[5], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[6], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[7], 0x1200000000000000u);

  EXPECT_EQ(out.zmm[31].v[0], 0x0000000000000043u);
  EXPECT_EQ(out.zmm[31].v[1], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[2], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[3], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[4], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[5], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[6], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[7], 0x1300000000000000u);

  EXPECT_EQ(out.mxcsr, 0x08070605u);
}

TEST(x64Helpers, WriteDebugRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr0, std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8});
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr1, std::vector<uint8_t>{2, 3, 4, 5, 6, 7, 8, 9});
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr2, std::vector<uint8_t>{3, 4, 5, 6, 7, 8, 9, 0});
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr3, std::vector<uint8_t>{4, 5, 6, 7, 8, 9, 0, 1});
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr6, std::vector<uint8_t>{5, 6, 7, 8, 9, 0, 1, 2});
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr7, std::vector<uint8_t>{6, 7, 8, 9, 0, 1, 2, 3});

  zx_thread_state_debug_regs_t out = {};
  zx_status_t res = WriteDebugRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.dr[0], 0x0807060504030201u);
  EXPECT_EQ(out.dr[1], 0x0908070605040302u);
  EXPECT_EQ(out.dr[2], 0x0009080706050403u);
  EXPECT_EQ(out.dr[3], 0x0100090807060504u);
  EXPECT_EQ(out.dr6, 0x0201000908070605u);
  EXPECT_EQ(out.dr7, 0x0302010009080706u);
}

// HW Breakpoints --------------------------------------------------------------

TEST(x64Helpers, SettingHWBreakpoints) {
  constexpr uint64_t kAddress1 = 0x0123;
  constexpr uint64_t kAddress2 = 0x4567;
  constexpr uint64_t kAddress3 = 0x89ab;
  constexpr uint64_t kAddress4 = 0xcdef;
  constexpr uint64_t kAddress5 = 0xdeadbeef;

  zx_thread_state_debug_regs_t debug_regs = {};

  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress1, ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], 0u);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], 0u);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0}));

  // Adding the same breakpoint should detect that the same already exists.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress1, ZX_ERR_ALREADY_BOUND);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], 0u);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], 0u);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0}));

  // Continuing adding should append.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress2, ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], 0u);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1}));

  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], kAddress3);
  EXPECT_EQ(debug_regs.dr[3], 0u);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2}));

  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress4, ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], kAddress3);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3}));

  // No more registers left should not change anything.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress5, ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], kAddress3);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3}));
}

TEST(x64Helpers, RemovingHWBreakpoint) {
  constexpr uint64_t kAddress1 = 0x0123;
  constexpr uint64_t kAddress2 = 0x4567;
  constexpr uint64_t kAddress3 = 0x89ab;
  constexpr uint64_t kAddress4 = 0xcdef;
  constexpr uint64_t kAddress5 = 0xdeadbeef;

  zx_thread_state_debug_regs_t debug_regs = {};

  // Previous state verifies the state of this calls.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress1, ZX_OK);
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress2, ZX_OK);
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_OK);
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress4, ZX_OK);
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress5, ZX_ERR_NO_RESOURCES);

  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 3}));

  // Removing same breakpoint should not work.
  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 3}));

  // Removing an unknown address should warn and change nothing.
  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, 0xaaaaaaa, ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(debug_regs.dr[0], kAddress1);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 3}));

  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress1, ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], 0u);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {1, 3}));

  // Adding again should work.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress5, ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress5);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], 0u);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 3}));

  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress1, ZX_OK);
  EXPECT_EQ(debug_regs.dr[0], kAddress5);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], kAddress1);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3}));

  // Already exists should not change.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress5, ZX_ERR_ALREADY_BOUND);
  EXPECT_EQ(debug_regs.dr[0], kAddress5);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], kAddress1);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3}));

  // No more resources.
  SetupHWBreakpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(debug_regs.dr[0], kAddress5);
  EXPECT_EQ(debug_regs.dr[1], kAddress2);
  EXPECT_EQ(debug_regs.dr[2], kAddress1);
  EXPECT_EQ(debug_regs.dr[3], kAddress4);
  EXPECT_EQ(debug_regs.dr6, 0u);
  EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3}));

  /* // Attempting to remove a watchpoint should not work. */
  /* RemoveWatchpointTest(FROM_HERE_NO_FUNC, &debug_regs, kAddress3, ZX_ERR_OUT_OF_RANGE); */
  /* EXPECT_EQ(debug_regs.dr[0], kAddress5); */
  /* EXPECT_EQ(debug_regs.dr[1], kAddress2); */
  /* EXPECT_EQ(debug_regs.dr[2], kAddress1); */
  /* EXPECT_EQ(debug_regs.dr[3], kAddress4); */
  /* EXPECT_EQ(debug_regs.dr6, 0u); */
  /* EXPECT_EQ(debug_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3})); */
}

// Watchpoints -------------------------------------------------------------------------------------

namespace {

bool CheckAddresses(const zx_thread_state_debug_regs_t& regs, std::vector<uint64_t> addresses) {
  FXL_DCHECK(addresses.size() == 4u);
  bool has_errors = false;
  for (int i = 0; i < 4; i++) {
    if (regs.dr[i] != addresses[i]) {
      ADD_FAILURE() << "Slot " << i << std::hex << ": Expected 0x" << addresses[i] << ", got: 0x"
                    << regs.dr[i];
      has_errors = true;
    }
  }

  return !has_errors;
}

bool CheckLengths(const zx_thread_state_debug_regs_t& regs, std::vector<uint64_t> lengths) {
  FXL_DCHECK(lengths.size() == 4u);
  bool has_errors = false;
  for (int i = 0; i < 4; i++) {
    uint64_t length = GetWatchpointLength(regs.dr7, i);
    if (length != lengths[i]) {
      ADD_FAILURE() << "Slot " << i << ": Expected " << lengths[i] << ", got: " << length;
      has_errors = true;
    }
  }

  return !has_errors;
}

bool CheckSetup(zx_thread_state_debug_regs_t* regs, uint64_t address, uint64_t size,
                WatchpointInstallationResult expected) {
  WatchpointInstallationResult result = SetupWatchpoint(regs, address, size);
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

bool CheckSetupWithReset(zx_thread_state_debug_regs_t* regs, uint64_t address, uint64_t size,
                         WatchpointInstallationResult expected) {
  // Restart the registers.
  *regs = {};
  return CheckSetup(regs, address, size, expected);
}

}  // namespace

TEST(x64Helpers_SettingWatchpoints, RangeValidation) {
  zx_thread_state_debug_regs_t regs = {};

  // Always aligned.
  constexpr uint64_t kAddress = 0x1000;

  // clang-format off
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress,  0, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress,  1, CreateResult(ZX_OK, {0x1000, 0x1001}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress,  2, CreateResult(ZX_OK, {0x1000, 0x1002}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress,  3, CreateResult(ZX_OK, {0x1000, 0x1004}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress,  4, CreateResult(ZX_OK, {0x1000, 0x1004}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress,  5, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress,  6, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress,  7, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress,  8, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress,  9, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  EXPECT_TRUE(CheckSetupWithReset(&regs, kAddress, 10, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  // clang-format on
}

TEST(x64Helpers_SettingWatchpoints, SetupMany) {
  zx_thread_state_debug_regs_t regs = {};

  // Always aligned address.
  constexpr uint64_t kAddress1 = 0x10000;
  constexpr uint64_t kAddress2 = 0x20000;
  constexpr uint64_t kAddress3 = 0x30000;
  constexpr uint64_t kAddress4 = 0x40000;
  constexpr uint64_t kAddress5 = 0x50000;

  ASSERT_TRUE(CheckSetup(&regs, kAddress1, 1, CreateResult(ZX_OK, {kAddress1, kAddress1 + 1}, 0)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress1, 1, CreateResult(ZX_ERR_ALREADY_BOUND)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress2, 2, CreateResult(ZX_OK, {kAddress2, kAddress2 + 2}, 1)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 1, 1}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress3, 4, CreateResult(ZX_OK, {kAddress3, kAddress3 + 4}, 2)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 1}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress4, 8, CreateResult(ZX_OK, {kAddress4, kAddress4 + 8}, 3)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress5, 8, CreateResult(ZX_ERR_NO_RESOURCES)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));

  ASSERT_ZX_EQ(RemoveWatchpoint(&regs, kAddress3, 4), ZX_OK);
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 1, 8}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress5, 8, CreateResult(ZX_OK, {kAddress5, kAddress5 + 8}, 2)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress5, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 8, 8}));

  ASSERT_ZX_EQ(RemoveWatchpoint(&regs, kAddress3, 4), ZX_ERR_NOT_FOUND);
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress5, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 8, 8}));
}

// clang-format off
TEST(x64Helpers_SettingWatchpoints, Alignment) {
  zx_thread_state_debug_regs_t regs = {};

  // 1-byte alignment.
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1000, 1, CreateResult(ZX_OK, {0x1000, 0x1001}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1001, 1, CreateResult(ZX_OK, {0x1001, 0x1002}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1002, 1, CreateResult(ZX_OK, {0x1002, 0x1003}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1003, 1, CreateResult(ZX_OK, {0x1003, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1004, 1, CreateResult(ZX_OK, {0x1004, 0x1005}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1005, 1, CreateResult(ZX_OK, {0x1005, 0x1006}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1006, 1, CreateResult(ZX_OK, {0x1006, 0x1007}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1007, 1, CreateResult(ZX_OK, {0x1007, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1008, 1, CreateResult(ZX_OK, {0x1008, 0x1009}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1009, 1, CreateResult(ZX_OK, {0x1009, 0x100a}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100a, 1, CreateResult(ZX_OK, {0x100a, 0x100b}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100b, 1, CreateResult(ZX_OK, {0x100b, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100c, 1, CreateResult(ZX_OK, {0x100c, 0x100d}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100d, 1, CreateResult(ZX_OK, {0x100d, 0x100e}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100e, 1, CreateResult(ZX_OK, {0x100e, 0x100f}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100f, 1, CreateResult(ZX_OK, {0x100f, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1010, 1, CreateResult(ZX_OK, {0x1010, 0x1011}, 0)));

  // 2-byte alignment.
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1000, 2, CreateResult(ZX_OK, {0x1000, 0x1002}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1001, 2, CreateResult(ZX_OK, {0x1000, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1002, 2, CreateResult(ZX_OK, {0x1002, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1003, 2, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1004, 2, CreateResult(ZX_OK, {0x1004, 0x1006}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1005, 2, CreateResult(ZX_OK, {0x1004, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1006, 2, CreateResult(ZX_OK, {0x1006, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1007, 2, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1008, 2, CreateResult(ZX_OK, {0x1008, 0x100a}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1009, 2, CreateResult(ZX_OK, {0x1008, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100a, 2, CreateResult(ZX_OK, {0x100a, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100b, 2, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100c, 2, CreateResult(ZX_OK, {0x100c, 0x100e}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100d, 2, CreateResult(ZX_OK, {0x100c, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100e, 2, CreateResult(ZX_OK, {0x100e, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100f, 2, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1010, 2, CreateResult(ZX_OK, {0x1010, 0x1012}, 0)));

  // 3-byte alignment.
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1000, 3, CreateResult(ZX_OK, {0x1000, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1001, 3, CreateResult(ZX_OK, {0x1000, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1002, 3, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1003, 3, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1004, 3, CreateResult(ZX_OK, {0x1004, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1005, 3, CreateResult(ZX_OK, {0x1004, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1006, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1007, 3, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1008, 3, CreateResult(ZX_OK, {0x1008, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1009, 3, CreateResult(ZX_OK, {0x1008, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100a, 3, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100b, 3, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));


  // 4 byte range.
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1000, 4, CreateResult(ZX_OK, {0x1000, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1001, 4, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1002, 4, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1003, 4, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1004, 4, CreateResult(ZX_OK, {0x1004, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1005, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1006, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1007, 4, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1008, 4, CreateResult(ZX_OK, {0x1008, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1009, 4, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100a, 4, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100b, 4, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100c, 4, CreateResult(ZX_OK, {0x100c, 0x1010}, 0)));

  // 5 byte range.
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1000, 5, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1001, 5, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1002, 5, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1003, 5, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1004, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1005, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1006, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1007, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1008, 5, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1009, 5, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100a, 5, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100b, 5, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100c, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100d, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100e, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100f, 5, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  // 6 byte range.
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1000, 6, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1001, 6, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1002, 6, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1003, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1004, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1005, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1006, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1007, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1008, 6, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1009, 6, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100a, 6, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100b, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100c, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100d, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100e, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100f, 6, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  // 7 byte range.
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1000, 7, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1001, 7, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1002, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1003, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1004, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1005, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1006, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1007, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1008, 7, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1009, 7, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100a, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100b, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100c, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100d, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100e, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100f, 7, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  // 8 byte range.
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1000, 8, CreateResult(ZX_OK, {0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1001, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1002, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1003, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1004, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1005, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1006, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1007, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));

  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1008, 8, CreateResult(ZX_OK, {0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x1009, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100a, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100b, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100c, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100d, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100e, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
  ASSERT_TRUE(CheckSetupWithReset(&regs, 0x100f, 8, CreateResult(ZX_ERR_OUT_OF_RANGE)));
}

// clang-format on

TEST(x64Helpers_SettingWatchpoints, RangeIsDifferentWatchpoint) {
  zx_thread_state_debug_regs_t regs = {};
  constexpr uint64_t kAddress = 0x10000;

  ASSERT_TRUE(CheckSetup(&regs, kAddress, 1, CreateResult(ZX_OK, {kAddress, kAddress + 1}, 0)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress, 1, CreateResult(ZX_ERR_ALREADY_BOUND)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress, 2, CreateResult(ZX_OK, {kAddress, kAddress + 2}, 1)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, kAddress, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 1, 1}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress, 2, CreateResult(ZX_ERR_ALREADY_BOUND)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, kAddress, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 1, 1}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress, 4, CreateResult(ZX_OK, {kAddress, kAddress + 4}, 2)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, kAddress, kAddress, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 1}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress, 4, CreateResult(ZX_ERR_ALREADY_BOUND)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, kAddress, kAddress, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 1}));

  ASSERT_TRUE(CheckSetup(&regs, kAddress, 8, CreateResult(ZX_OK, {kAddress, kAddress + 8}, 3)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, kAddress, kAddress, kAddress}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));

  // Deleting is by range too.
  ASSERT_ZX_EQ(RemoveWatchpoint(&regs, kAddress, 2), ZX_OK);
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, 0, kAddress, kAddress}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 8}));

  ASSERT_ZX_EQ(RemoveWatchpoint(&regs, kAddress, 2), ZX_ERR_NOT_FOUND);
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, 0, kAddress, kAddress}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 8}));

  ASSERT_ZX_EQ(RemoveWatchpoint(&regs, kAddress, 1), ZX_OK);
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, kAddress, kAddress}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 8}));

  ASSERT_ZX_EQ(RemoveWatchpoint(&regs, kAddress, 1), ZX_ERR_NOT_FOUND);
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, kAddress, kAddress}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 8}));

  ASSERT_ZX_EQ(RemoveWatchpoint(&regs, kAddress, 8), ZX_OK);
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, kAddress, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 1}));

  ASSERT_ZX_EQ(RemoveWatchpoint(&regs, kAddress, 8), ZX_ERR_NOT_FOUND);
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, kAddress, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 1}));

  ASSERT_ZX_EQ(RemoveWatchpoint(&regs, kAddress, 4), ZX_OK);
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));
}

}  // namespace arch
}  // namespace debug_agent
