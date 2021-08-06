// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/hw/debug/x86.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/debug_registers.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/developer/debug/shared/logging/file_line_function.h"

namespace debug_agent {

namespace {

uint64_t X86LenToLength(uint64_t len) {
  // clang-format off
  switch (len) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 8;
    case 3: return 4;
  }
  // clang-format on

  FX_NOTREACHED() << "Invalid len: " << len;
  return 0;
}

uint64_t GetWatchpointLength(uint64_t dr7, int slot) {
  // clang-format off
  switch (slot) {
    case 0: return X86LenToLength(X86_DBG_CONTROL_LEN0_GET(dr7));
    case 1: return X86LenToLength(X86_DBG_CONTROL_LEN1_GET(dr7));
    case 2: return X86LenToLength(X86_DBG_CONTROL_LEN2_GET(dr7));
    case 3: return X86LenToLength(X86_DBG_CONTROL_LEN3_GET(dr7));
  }
  // clang-format on

  FX_NOTREACHED() << "Invalid slot: " << slot;
  return -1;
}

void SetHWBreakpointTest(debug::FileLineFunction file_line, DebugRegisters& debug_regs,
                         uint64_t address, bool expected_result) {
  bool result = debug_regs.SetHWBreakpoint(address);
  ASSERT_EQ(result, expected_result) << "[" << file_line.ToString() << "] "
                                     << "Got: " << result << ", expected: " << expected_result;
}

void RemoveHWBreakpointTest(debug::FileLineFunction file_line, DebugRegisters& debug_regs,
                            uint64_t address, bool expected_result) {
  bool result = debug_regs.RemoveHWBreakpoint(address);
  ASSERT_EQ(result, expected_result) << "[" << file_line.ToString() << "] "
                                     << "Got: " << result << ", expected: " << expected_result;
}

uint64_t GetHWBreakpointDR7Mask(size_t index) {
  FX_DCHECK(index < 4);
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
    FX_DCHECK(index < 4);
    val |= GetHWBreakpointDR7Mask(index);
  }

  return val;
}

constexpr uint32_t kWrite = 0b1;
constexpr uint32_t kReadWrite = 0b11;

bool CheckAddresses(const DebugRegisters& regs, std::vector<uint64_t> addresses) {
  FX_DCHECK(addresses.size() == 4u);
  bool has_errors = false;
  for (int i = 0; i < 4; i++) {
    if (regs.GetNativeRegisters().dr[i] != addresses[i]) {
      ADD_FAILURE() << "Slot " << i << std::hex << ": Expected 0x" << addresses[i] << ", got: 0x"
                    << regs.GetNativeRegisters().dr[i];
      has_errors = true;
    }
  }

  return !has_errors;
}

bool CheckLengths(const DebugRegisters& regs, std::vector<uint64_t> lengths) {
  FX_DCHECK(lengths.size() == 4u);
  bool has_errors = false;
  for (int i = 0; i < 4; i++) {
    uint64_t length = GetWatchpointLength(regs.GetNativeRegisters().dr7, i);
    if (length != lengths[i]) {
      ADD_FAILURE() << "Slot " << i << ": Expected " << lengths[i] << ", got: " << length;
      has_errors = true;
    }
  }

  return !has_errors;
}

uint32_t GetWatchpointRW(uint64_t dr7, int slot) {
  // clang-format off
  switch (slot) {
    case 0: return X86_DBG_CONTROL_RW0_GET(dr7);
    case 1: return X86_DBG_CONTROL_RW1_GET(dr7);
    case 2: return X86_DBG_CONTROL_RW2_GET(dr7);
    case 3: return X86_DBG_CONTROL_RW3_GET(dr7);
  }
  // clang-format on

  FX_NOTREACHED() << "Invalid slot: " << slot;
  return -1;
}

bool CheckTypes(const DebugRegisters& regs, std::vector<uint32_t> rws) {
  FX_DCHECK(rws.size() == 4u);
  bool has_errors = false;
  for (int i = 0; i < 4; i++) {
    uint32_t rw = GetWatchpointRW(regs.GetNativeRegisters().dr7, i);
    if (rw != rws[i]) {
      ADD_FAILURE() << "Slot RW" << i << ": Expected " << rws[i] << ", got: " << rw;
      has_errors = true;
    }
  }

  return !has_errors;
}

bool CheckSetup(DebugRegisters& regs, uint64_t address, uint64_t size,
                std::optional<WatchpointInfo> expected,
                debug_ipc::BreakpointType type = debug_ipc::BreakpointType::kWrite) {
  auto result = regs.SetWatchpoint(type, {address, address + size}, 4);
  if (result != expected) {
    ADD_FAILURE() << "Mismatched watchpoint.";
    return false;
  }
  return true;
}

bool CheckSetupWithReset(DebugRegisters& regs, uint64_t address, uint64_t size,
                         std::optional<WatchpointInfo> expected) {
  // Restart the registers.
  regs = DebugRegisters();
  return CheckSetup(regs, address, size, expected);
}

}  // namespace

TEST(DebugRegistersX64, SetHWBreakpoints) {
  constexpr uint64_t kAddress1 = 0x0123;
  constexpr uint64_t kAddress2 = 0x4567;
  constexpr uint64_t kAddress3 = 0x89ab;
  constexpr uint64_t kAddress4 = 0xcdef;
  constexpr uint64_t kAddress5 = 0xdeadbeef;

  DebugRegisters debug_regs;
  auto& native_regs = debug_regs.GetNativeRegisters();

  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress1, true);
  EXPECT_EQ(native_regs.dr[0], kAddress1);
  EXPECT_EQ(native_regs.dr[1], 0u);
  EXPECT_EQ(native_regs.dr[2], 0u);
  EXPECT_EQ(native_regs.dr[3], 0u);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0}));

  // Adding the same breakpoint should detect that the same already exists.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress1, false);
  EXPECT_EQ(native_regs.dr[0], kAddress1);
  EXPECT_EQ(native_regs.dr[1], 0u);
  EXPECT_EQ(native_regs.dr[2], 0u);
  EXPECT_EQ(native_regs.dr[3], 0u);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0}));

  // Continuing adding should append.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress2, true);
  EXPECT_EQ(native_regs.dr[0], kAddress1);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], 0u);
  EXPECT_EQ(native_regs.dr[3], 0u);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1}));

  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, true);
  EXPECT_EQ(native_regs.dr[0], kAddress1);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], kAddress3);
  EXPECT_EQ(native_regs.dr[3], 0u);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2}));

  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress4, true);
  EXPECT_EQ(native_regs.dr[0], kAddress1);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], kAddress3);
  EXPECT_EQ(native_regs.dr[3], kAddress4);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3}));

  // No more registers left should not change anything.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress5, false);
  EXPECT_EQ(native_regs.dr[0], kAddress1);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], kAddress3);
  EXPECT_EQ(native_regs.dr[3], kAddress4);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3}));
}

TEST(DebugRegistersX64, RemoveHWBreakpoint) {
  constexpr uint64_t kAddress1 = 0x0123;
  constexpr uint64_t kAddress2 = 0x4567;
  constexpr uint64_t kAddress3 = 0x89ab;
  constexpr uint64_t kAddress4 = 0xcdef;
  constexpr uint64_t kAddress5 = 0xdeadbeef;

  DebugRegisters debug_regs;
  auto& native_regs = debug_regs.GetNativeRegisters();

  // Previous state verifies the state of this calls.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress1, true);
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress2, true);
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, true);
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress4, true);
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress5, false);

  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, true);
  EXPECT_EQ(native_regs.dr[0], kAddress1);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], 0u);
  EXPECT_EQ(native_regs.dr[3], kAddress4);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 3}));

  // Removing same breakpoint should not work.
  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, false);
  EXPECT_EQ(native_regs.dr[0], kAddress1);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], 0u);
  EXPECT_EQ(native_regs.dr[3], kAddress4);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 3}));

  // Removing an unknown address should warn and change nothing.
  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, 0xaaaaaaa, false);
  EXPECT_EQ(native_regs.dr[0], kAddress1);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], 0u);
  EXPECT_EQ(native_regs.dr[3], kAddress4);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 3}));

  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress1, true);
  EXPECT_EQ(native_regs.dr[0], 0u);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], 0u);
  EXPECT_EQ(native_regs.dr[3], kAddress4);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {1, 3}));

  // Adding again should work.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress5, true);
  EXPECT_EQ(native_regs.dr[0], kAddress5);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], 0u);
  EXPECT_EQ(native_regs.dr[3], kAddress4);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 3}));

  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress1, true);
  EXPECT_EQ(native_regs.dr[0], kAddress5);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], kAddress1);
  EXPECT_EQ(native_regs.dr[3], kAddress4);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3}));

  // Already exists should not change.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress5, false);
  EXPECT_EQ(native_regs.dr[0], kAddress5);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], kAddress1);
  EXPECT_EQ(native_regs.dr[3], kAddress4);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3}));

  // No more resources.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, false);
  EXPECT_EQ(native_regs.dr[0], kAddress5);
  EXPECT_EQ(native_regs.dr[1], kAddress2);
  EXPECT_EQ(native_regs.dr[2], kAddress1);
  EXPECT_EQ(native_regs.dr[3], kAddress4);
  EXPECT_EQ(native_regs.dr6, 0u);
  EXPECT_EQ(native_regs.dr7, JoinDR7HWBreakpointMask(0, {0, 1, 2, 3}));
}

TEST(DebugRegistersX64, WatchpointRangeValidation) {
  DebugRegisters regs;

  // Always aligned.
  constexpr uint64_t kAddress = 0x1000;

  // clang-format off
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress,  0, std::nullopt));
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress,  1, WatchpointInfo({0x1000, 0x1001}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress,  2, WatchpointInfo({0x1000, 0x1002}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress,  3, WatchpointInfo({0x1000, 0x1004}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress,  4, WatchpointInfo({0x1000, 0x1004}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress,  5, WatchpointInfo({0x1000, 0x1008}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress,  6, WatchpointInfo({0x1000, 0x1008}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress,  7, WatchpointInfo({0x1000, 0x1008}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress,  8, WatchpointInfo({0x1000, 0x1008}, 0)));
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress,  9, std::nullopt));
  EXPECT_TRUE(CheckSetupWithReset(regs, kAddress, 10, std::nullopt));
  // clang-format on
}

TEST(DebugRegistersX64, SetupManyWatchpoints) {
  DebugRegisters regs;

  // Always aligned address.
  constexpr uint64_t kAddress1 = 0x10000;
  constexpr uint64_t kAddress2 = 0x20000;
  constexpr uint64_t kAddress3 = 0x30000;
  constexpr uint64_t kAddress4 = 0x40000;
  constexpr uint64_t kAddress5 = 0x50000;

  ASSERT_TRUE(CheckSetup(regs, kAddress1, 1, WatchpointInfo({kAddress1, kAddress1 + 1}, 0)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress1, 1, std::nullopt));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress2, 2, WatchpointInfo({kAddress2, kAddress2 + 2}, 1)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress3, 4, WatchpointInfo({kAddress3, kAddress3 + 4}, 2)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress4, 8, WatchpointInfo({kAddress4, kAddress4 + 8}, 3)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  ASSERT_TRUE(CheckSetup(regs, kAddress5, 8, std::nullopt));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  ASSERT_TRUE(regs.RemoveWatchpoint({kAddress3, kAddress3 + 4}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 1, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, kWrite}));

  ASSERT_TRUE(CheckSetup(regs, kAddress5, 8, WatchpointInfo({kAddress5, kAddress5 + 8}, 2)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress5, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 8, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  ASSERT_FALSE(regs.RemoveWatchpoint({kAddress3, kAddress3 + 4}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress5, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 8, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));
}

// clang-format off
TEST(DebugRegistersX64, Alignment) {
  DebugRegisters regs;

  // 1-byte alignment.
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1000, 1, WatchpointInfo({0x1000, 0x1001}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1001, 1, WatchpointInfo({0x1001, 0x1002}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1002, 1, WatchpointInfo({0x1002, 0x1003}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1003, 1, WatchpointInfo({0x1003, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1004, 1, WatchpointInfo({0x1004, 0x1005}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1005, 1, WatchpointInfo({0x1005, 0x1006}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1006, 1, WatchpointInfo({0x1006, 0x1007}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1007, 1, WatchpointInfo({0x1007, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1008, 1, WatchpointInfo({0x1008, 0x1009}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1009, 1, WatchpointInfo({0x1009, 0x100a}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100a, 1, WatchpointInfo({0x100a, 0x100b}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100b, 1, WatchpointInfo({0x100b, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100c, 1, WatchpointInfo({0x100c, 0x100d}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100d, 1, WatchpointInfo({0x100d, 0x100e}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100e, 1, WatchpointInfo({0x100e, 0x100f}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100f, 1, WatchpointInfo({0x100f, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1010, 1, WatchpointInfo({0x1010, 0x1011}, 0)));

  // 2-byte alignment.
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1000, 2, WatchpointInfo({0x1000, 0x1002}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1001, 2, WatchpointInfo({0x1000, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1002, 2, WatchpointInfo({0x1002, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1003, 2, WatchpointInfo({0x1000, 0x1008}, 0)));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1004, 2, WatchpointInfo({0x1004, 0x1006}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1005, 2, WatchpointInfo({0x1004, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1006, 2, WatchpointInfo({0x1006, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1007, 2, std::nullopt));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1008, 2, WatchpointInfo({0x1008, 0x100a}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1009, 2, WatchpointInfo({0x1008, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100a, 2, WatchpointInfo({0x100a, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100b, 2, WatchpointInfo({0x1008, 0x1010}, 0)));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100c, 2, WatchpointInfo({0x100c, 0x100e}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100d, 2, WatchpointInfo({0x100c, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100e, 2, WatchpointInfo({0x100e, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100f, 2, std::nullopt));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1010, 2, WatchpointInfo({0x1010, 0x1012}, 0)));

  // 3-byte alignment.
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1000, 3, WatchpointInfo({0x1000, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1001, 3, WatchpointInfo({0x1000, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1002, 3, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1003, 3, WatchpointInfo({0x1000, 0x1008}, 0)));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1004, 3, WatchpointInfo({0x1004, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1005, 3, WatchpointInfo({0x1004, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1006, 3, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1007, 3, std::nullopt));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1008, 3, WatchpointInfo({0x1008, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1009, 3, WatchpointInfo({0x1008, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100a, 3, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100b, 3, WatchpointInfo({0x1008, 0x1010}, 0)));


  // 4 byte range.
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1000, 4, WatchpointInfo({0x1000, 0x1004}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1001, 4, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1002, 4, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1003, 4, WatchpointInfo({0x1000, 0x1008}, 0)));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1004, 4, WatchpointInfo({0x1004, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1005, 4, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1006, 4, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1007, 4, std::nullopt));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1008, 4, WatchpointInfo({0x1008, 0x100c}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1009, 4, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100a, 4, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100b, 4, WatchpointInfo({0x1008, 0x1010}, 0)));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100c, 4, WatchpointInfo({0x100c, 0x1010}, 0)));

  // 5 byte range.
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1000, 5, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1001, 5, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1002, 5, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1003, 5, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1004, 5, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1005, 5, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1006, 5, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1007, 5, std::nullopt));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1008, 5, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1009, 5, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100a, 5, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100b, 5, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100c, 5, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100d, 5, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100e, 5, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100f, 5, std::nullopt));

  // 6 byte range.
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1000, 6, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1001, 6, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1002, 6, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1003, 6, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1004, 6, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1005, 6, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1006, 6, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1007, 6, std::nullopt));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1008, 6, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1009, 6, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100a, 6, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100b, 6, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100c, 6, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100d, 6, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100e, 6, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100f, 6, std::nullopt));

  // 7 byte range.
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1000, 7, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1001, 7, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1002, 7, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1003, 7, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1004, 7, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1005, 7, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1006, 7, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1007, 7, std::nullopt));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1008, 7, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1009, 7, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100a, 7, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100b, 7, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100c, 7, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100d, 7, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100e, 7, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100f, 7, std::nullopt));

  // 8 byte range.
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1000, 8, WatchpointInfo({0x1000, 0x1008}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1001, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1002, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1003, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1004, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1005, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1006, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1007, 8, std::nullopt));

  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1008, 8, WatchpointInfo({0x1008, 0x1010}, 0)));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x1009, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100a, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100b, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100c, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100d, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100e, 8, std::nullopt));
  ASSERT_TRUE(CheckSetupWithReset(regs, 0x100f, 8, std::nullopt));
}

// clang-format on

TEST(DebugRegistersX64, RangeIsDifferentWatchpoint) {
  DebugRegisters regs;
  constexpr uint64_t kAddress = 0x10000;

  ASSERT_TRUE(CheckSetup(regs, kAddress, 1, WatchpointInfo({kAddress, kAddress + 1}, 0)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress, 1, std::nullopt));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress, 2, WatchpointInfo({kAddress, kAddress + 2}, 1)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, kAddress, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress, 2, std::nullopt));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, kAddress, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress, 4, WatchpointInfo({kAddress, kAddress + 4}, 2)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, kAddress, kAddress, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress, 4, std::nullopt));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, kAddress, kAddress, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress, 8, WatchpointInfo({kAddress, kAddress + 8}, 3)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, kAddress, kAddress, kAddress}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  // Deleting is by range too.
  ASSERT_TRUE(regs.RemoveWatchpoint({kAddress, kAddress + 2}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, 0, kAddress, kAddress}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, kWrite, kWrite}));

  ASSERT_FALSE(regs.RemoveWatchpoint({kAddress, kAddress + 2}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress, 0, kAddress, kAddress}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, kWrite, kWrite}));

  ASSERT_TRUE(regs.RemoveWatchpoint({kAddress, kAddress + 1}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, kAddress, kAddress}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, kWrite, kWrite}));

  ASSERT_FALSE(regs.RemoveWatchpoint({kAddress, kAddress + 1}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, kAddress, kAddress}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, kWrite, kWrite}));

  ASSERT_TRUE(regs.RemoveWatchpoint({kAddress, kAddress + 8}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, kAddress, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 1}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, kWrite, 0}));

  ASSERT_FALSE(regs.RemoveWatchpoint({kAddress, kAddress + 8}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, kAddress, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 4, 1}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, kWrite, 0}));

  ASSERT_TRUE(regs.RemoveWatchpoint({kAddress, kAddress + 4}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, 0, 0}));
}

TEST(DebugRegistersX64, DifferentWatchpointTypes) {
  DebugRegisters regs;

  // Always aligned address.
  constexpr uint64_t kAddress1 = 0x10000;
  constexpr uint64_t kAddress2 = 0x20000;
  constexpr uint64_t kAddress3 = 0x30000;
  constexpr uint64_t kAddress4 = 0x40000;
  constexpr uint64_t kAddress5 = 0x50000;

  ASSERT_TRUE(CheckSetup(regs, kAddress1, 1, WatchpointInfo({kAddress1, kAddress1 + 1}, 0),
                         debug_ipc::BreakpointType::kReadWrite));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kReadWrite, 0, 0, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress1, 1, std::nullopt));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kReadWrite, 0, 0, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress2, 2, WatchpointInfo({kAddress2, kAddress2 + 2}, 1),
                         debug_ipc::BreakpointType::kWrite));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 1, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kReadWrite, kWrite, 0, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress3, 4, WatchpointInfo({kAddress3, kAddress3 + 4}, 2),
                         debug_ipc::BreakpointType::kReadWrite));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 1}));
  EXPECT_TRUE(CheckTypes(regs, {kReadWrite, kWrite, kReadWrite, 0}));

  ASSERT_TRUE(CheckSetup(regs, kAddress4, 8, WatchpointInfo({kAddress4, kAddress4 + 8}, 3)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kReadWrite, kWrite, kReadWrite, kWrite}));

  ASSERT_TRUE(CheckSetup(regs, kAddress5, 8, std::nullopt));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kReadWrite, kWrite, kReadWrite, kWrite}));

  ASSERT_TRUE(regs.RemoveWatchpoint({kAddress3, kAddress3 + 4}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 1, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kReadWrite, kWrite, 0, kWrite}));

  ASSERT_TRUE(CheckSetup(regs, kAddress5, 8, WatchpointInfo({kAddress5, kAddress5 + 8}, 2)));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress5, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 8, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kReadWrite, kWrite, kWrite, kWrite}));

  ASSERT_FALSE(regs.RemoveWatchpoint({kAddress3, kAddress3 + 4}, 4));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress5, kAddress4}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 8, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kReadWrite, kWrite, kWrite, kWrite}));
}

}  // namespace debug_agent
