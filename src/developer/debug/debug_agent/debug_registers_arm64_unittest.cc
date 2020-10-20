// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/hw/debug/arm64.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/debug_registers.h"
#include "src/developer/debug/shared/logging/file_line_function.h"

namespace debug_agent {

constexpr uint64_t kDbgbvrE = 1u;
constexpr uint32_t kWatchpointCount = 4;

DebugRegisters GetDefaultRegs() {
  DebugRegisters result;
  result.GetNativeRegisters().hw_bps_count = 4;
  return result;
}

bool CheckAddresses(const DebugRegisters& regs, std::vector<uint64_t> addresses) {
  for (uint32_t i = 0; i < addresses.size(); i++) {
    if (regs.GetNativeRegisters().hw_wps[i].dbgwvr != addresses[i]) {
      ADD_FAILURE() << "Reg " << i << " mismatch. Expected: 0x" << std::hex << addresses[i]
                    << ", got: " << regs.GetNativeRegisters().hw_wps[i].dbgwvr;
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
      FX_NOTREACHED() << "Invalid bas: 0x" << std::hex << bas;
      return 0;
  }
}

bool CheckLengths(const DebugRegisters& regs, std::vector<uint32_t> lengths) {
  for (uint32_t i = 0; i < lengths.size(); i++) {
    uint32_t bas = ARM64_DBGWCR_BAS_GET(regs.GetNativeRegisters().hw_wps[i].dbgwcr);
    uint32_t length = CountBASBits(bas);

    if (length != lengths[i]) {
      ADD_FAILURE() << "Reg " << i << " wrong length. Expected " << lengths[i]
                    << ", got: " << length;
      return false;
    }
  }

  return true;
}

bool CheckEnabled(const DebugRegisters& regs, std::vector<uint32_t> enabled) {
  for (uint32_t i = 0; i < enabled.size(); i++) {
    uint32_t e = ARM64_DBGWCR_E_GET(regs.GetNativeRegisters().hw_wps[i].dbgwcr);
    if (e != enabled[i]) {
      ADD_FAILURE() << "Reg " << i << " wrong enable. Expected: " << enabled[i] << ", got: " << e;
      return false;
    }
  }

  return true;
}

// Breakpoint types.
// constexpr uint32_t kRead = 0b01;  (unused).
constexpr uint32_t kWrite = 0b10;
constexpr uint32_t kReadWrite = 0b11;

bool CheckTypes(const DebugRegisters& regs, std::vector<uint32_t> types) {
  for (uint32_t i = 0; i < types.size(); i++) {
    uint32_t type = ARM64_DBGWCR_LSC_GET(regs.GetNativeRegisters().hw_wps[i].dbgwcr);
    if (type != types[i]) {
      ADD_FAILURE() << "Reg " << i << " wrong type. Expected: " << types[i] << ", got: " << type;
      return false;
    }
  }

  return true;
}

bool ResultVerification(DebugRegisters& regs, uint64_t address, uint64_t size,
                        debug_ipc::BreakpointType type,
                        const std::optional<WatchpointInfo>& expected) {
  std::optional<WatchpointInfo> result =
      regs.SetWatchpoint(type, {address, address + size}, kWatchpointCount);
  if (result != expected) {
    ADD_FAILURE() << "Mismatch watchpoint.";
    return false;
  }
  return true;
}

bool Check(DebugRegisters& regs, uint64_t address, uint64_t size, debug_ipc::BreakpointType type,
           std::optional<WatchpointInfo> expected, uint32_t expected_bas = 0) {
  if (!ResultVerification(regs, address, size, type, expected))
    return false;

  // If no installation was made, we don't compare against BAS.
  if (!expected || expected->slot == -1)
    return true;

  uint32_t bas = ARM64_DBGWCR_BAS_GET(regs.GetNativeRegisters().hw_wps[expected->slot].dbgwcr);
  if (bas != expected_bas) {
    ADD_FAILURE() << "BAS check failed. Expected: 0x" << std::hex << expected_bas << ", got: 0x"
                  << bas;
    return false;
  }

  return true;
}

bool ResetCheck(DebugRegisters& regs, uint64_t address, uint64_t size,
                debug_ipc::BreakpointType type, std::optional<WatchpointInfo> expected,
                uint32_t expected_bas = 0) {
  // Restart the registers.
  regs = DebugRegisters();
  return Check(regs, address, size, type, expected, expected_bas);
}

void SetHWBreakpointTest(debug_ipc::FileLineFunction file_line, DebugRegisters& debug_regs,
                         uint64_t address, bool expected_result) {
  bool result = debug_regs.SetHWBreakpoint(address);
  ASSERT_EQ(result, expected_result) << "[" << file_line.ToString() << "] "
                                     << "Got: " << result << ", expected: " << expected_result;
}

void RemoveHWBreakpointTest(debug_ipc::FileLineFunction file_line, DebugRegisters& debug_regs,
                            uint64_t address, bool expected_result) {
  bool result = debug_regs.RemoveHWBreakpoint(address);
  ASSERT_EQ(result, expected_result) << "[" << file_line.ToString() << "] "
                                     << "Got: " << result << ", expected: " << expected_result;
}

// Always aligned address.
constexpr uint64_t kAddress1 = 0x10000;
constexpr uint64_t kAddress2 = 0x20000;
constexpr uint64_t kAddress3 = 0x30000;
constexpr uint64_t kAddress4 = 0x40000;
constexpr uint64_t kAddress5 = 0x50000;

TEST(DebugRegistersArm64, SettingBreakpoints) {
  auto debug_regs = GetDefaultRegs();
  auto& native_regs = debug_regs.GetNativeRegisters();

  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress1, true);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress1);
  for (size_t i = 1; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Adding the same breakpoint should detect that the same already exists.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress1, true);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress1);
  for (size_t i = 1; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Continuing adding should append.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress2, true);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  for (size_t i = 2; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, true);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, kAddress3);
  for (size_t i = 3; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress4, true);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, kAddress3);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  // No more registers left should not change anything.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress5, false);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, kAddress3);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }
}

TEST(DebugRegistersArm64, Removing) {
  auto debug_regs = GetDefaultRegs();
  auto& native_regs = debug_regs.GetNativeRegisters();

  // Previous state verifies the state of this calls.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress1, true);
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress2, true);
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, true);
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress4, true);
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress5, false);

  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, true);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, 0u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Removing same breakpoint should not work.
  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, false);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, 0u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Removing an unknown address should warn and change nothing.
  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, 0xaaaaaaa, false);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, 0u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  RemoveHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress1, true);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, 0u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, 0u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Adding again should work.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress5, true);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress5);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 0u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, 0u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress1, true);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress5);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  // Already exists should not change anything.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress5, true);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress5);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, false);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress5);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }

  // No more registers.
  SetHWBreakpointTest(FROM_HERE_NO_FUNC, debug_regs, kAddress3, false);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[0].dbgbvr, kAddress5);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[1].dbgbvr, kAddress2);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[2].dbgbvr, kAddress1);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbcr & kDbgbvrE, 1u);
  EXPECT_EQ(native_regs.hw_bps[3].dbgbvr, kAddress4);
  for (size_t i = 4; i < std::size(native_regs.hw_bps); i++) {
    EXPECT_EQ(native_regs.hw_bps[i].dbgbcr & kDbgbvrE, 0u);
    EXPECT_EQ(native_regs.hw_bps[i].dbgbvr, 0u);
  }
}

TEST(DebugRegistersArm64, SetupMany) {
  DebugRegisters regs;

  ASSERT_TRUE(Check(regs, kAddress1, 1, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({kAddress1, kAddress1 + 1}, 0), 0x1));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(Check(regs, kAddress1, 1, debug_ipc::BreakpointType::kWrite, std::nullopt));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(Check(regs, kAddress2, 2, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({kAddress2, kAddress2 + 2}, 1), 0x3));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, 0}));

  ASSERT_TRUE(Check(regs, kAddress3, 4, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({kAddress3, kAddress3 + 4}, 2), 0xf));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, 0}));

  ASSERT_TRUE(Check(regs, kAddress4, 8, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({kAddress4, kAddress4 + 8}, 3), 0xff));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  ASSERT_TRUE(Check(regs, kAddress5, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  ASSERT_TRUE(regs.RemoveWatchpoint({kAddress3, kAddress3 + 4}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 0, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 0, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, kWrite}));

  ASSERT_TRUE(Check(regs, kAddress5, 8, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({kAddress5, kAddress5 + 8}, 2), 0xff));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress5, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 8, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  ASSERT_FALSE(regs.RemoveWatchpoint({kAddress3, kAddress3 + 4}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress5, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 8, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));
}

TEST(DebugRegistersArm64, Ranges) {
  DebugRegisters regs;

  // 1-byte alignment.
  ASSERT_TRUE(ResetCheck(regs, 0x1000, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1000, 0x1001}, 0), 0b00000001));
  ASSERT_TRUE(ResetCheck(regs, 0x1001, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1001, 0x1002}, 0), 0b00000010));
  ASSERT_TRUE(ResetCheck(regs, 0x1002, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1002, 0x1003}, 0), 0b00000100));
  ASSERT_TRUE(ResetCheck(regs, 0x1003, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1003, 0x1004}, 0), 0b00001000));
  ASSERT_TRUE(ResetCheck(regs, 0x1004, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1004, 0x1005}, 0), 0b00000001));
  ASSERT_TRUE(ResetCheck(regs, 0x1005, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1005, 0x1006}, 0), 0b00000010));
  ASSERT_TRUE(ResetCheck(regs, 0x1006, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1006, 0x1007}, 0), 0b00000100));
  ASSERT_TRUE(ResetCheck(regs, 0x1007, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1007, 0x1008}, 0), 0b00001000));
  ASSERT_TRUE(ResetCheck(regs, 0x1008, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1008, 0x1009}, 0), 0b00000001));
  ASSERT_TRUE(ResetCheck(regs, 0x1009, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1009, 0x100a}, 0), 0b00000010));
  ASSERT_TRUE(ResetCheck(regs, 0x100a, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x100a, 0x100b}, 0), 0b00000100));
  ASSERT_TRUE(ResetCheck(regs, 0x100b, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x100b, 0x100c}, 0), 0b00001000));
  ASSERT_TRUE(ResetCheck(regs, 0x100c, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x100c, 0x100d}, 0), 0b00000001));
  ASSERT_TRUE(ResetCheck(regs, 0x100d, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x100d, 0x100e}, 0), 0b00000010));
  ASSERT_TRUE(ResetCheck(regs, 0x100e, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x100e, 0x100f}, 0), 0b00000100));
  ASSERT_TRUE(ResetCheck(regs, 0x100f, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x100f, 0x1010}, 0), 0b00001000));
  ASSERT_TRUE(ResetCheck(regs, 0x1010, 1, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1010, 0x1011}, 0), 0b00000001));

  // 2-byte alignment.
  ASSERT_TRUE(ResetCheck(regs, 0x1000, 2, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1000, 0x1002}, 0), 0b00000011));
  ASSERT_TRUE(ResetCheck(regs, 0x1001, 2, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1002, 2, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1002, 0x1004}, 0), 0b00001100));
  ASSERT_TRUE(ResetCheck(regs, 0x1003, 2, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1004, 2, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1004, 0x1006}, 0), 0b00000011));
  ASSERT_TRUE(ResetCheck(regs, 0x1005, 2, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1006, 2, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1006, 0x1008}, 0), 0b00001100));
  ASSERT_TRUE(ResetCheck(regs, 0x1007, 2, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1008, 2, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1008, 0x100a}, 0), 0b00000011));
  ASSERT_TRUE(ResetCheck(regs, 0x1009, 2, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100a, 2, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x100a, 0x100c}, 0), 0b00001100));
  ASSERT_TRUE(ResetCheck(regs, 0x100b, 2, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x100c, 2, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x100c, 0x100e}, 0), 0b00000011));
  ASSERT_TRUE(ResetCheck(regs, 0x100d, 2, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100e, 2, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x100e, 0x1010}, 0), 0b00001100));
  ASSERT_TRUE(ResetCheck(regs, 0x100f, 2, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1010, 2, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1010, 0x1012}, 0), 0b00000011));

  // 3-byte alignment.
  ASSERT_TRUE(ResetCheck(regs, 0x1000, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1001, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1002, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1003, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1004, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1005, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1006, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1007, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1008, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1009, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100a, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100b, 3, debug_ipc::BreakpointType::kWrite, std::nullopt));

  // 4 byte range.
  ASSERT_TRUE(ResetCheck(regs, 0x1000, 4, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1000, 0x1004}, 0), 0x0f));
  ASSERT_TRUE(ResetCheck(regs, 0x1001, 4, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1002, 4, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1003, 4, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1004, 4, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1004, 0x1008}, 0), 0x0f));
  ASSERT_TRUE(ResetCheck(regs, 0x1005, 4, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1006, 4, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1007, 4, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1008, 4, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1008, 0x100c}, 0), 0x0f));
  ASSERT_TRUE(ResetCheck(regs, 0x1009, 4, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100a, 4, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100b, 4, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x100c, 4, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x100c, 0x1010}, 0), 0x0f));

  // 5 byte range.
  ASSERT_TRUE(ResetCheck(regs, 0x1000, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1001, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1002, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1003, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1004, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1005, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1006, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1007, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1008, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1009, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100a, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100b, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100c, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100d, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100e, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100f, 5, debug_ipc::BreakpointType::kWrite, std::nullopt));

  // 6 byte range.
  ASSERT_TRUE(ResetCheck(regs, 0x1000, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1001, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1002, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1003, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1004, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1005, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1006, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1007, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1008, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1009, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100a, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100b, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100c, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100d, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100e, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100f, 6, debug_ipc::BreakpointType::kWrite, std::nullopt));

  // 7 byte range.
  ASSERT_TRUE(ResetCheck(regs, 0x1000, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1001, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1002, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1003, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1004, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1005, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1006, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1007, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1008, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1009, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100a, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100b, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100c, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100d, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100e, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100f, 7, debug_ipc::BreakpointType::kWrite, std::nullopt));

  // 8 byte range.
  ASSERT_TRUE(ResetCheck(regs, 0x1000, 8, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1000, 0x1008}, 0), 0xff));
  ASSERT_TRUE(ResetCheck(regs, 0x1001, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1002, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1003, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1004, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1005, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1006, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x1007, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));

  ASSERT_TRUE(ResetCheck(regs, 0x1008, 8, debug_ipc::BreakpointType::kWrite,
                         WatchpointInfo({0x1008, 0x1010}, 0), 0xff));
  ASSERT_TRUE(ResetCheck(regs, 0x1009, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100a, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100b, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100c, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100d, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100e, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(ResetCheck(regs, 0x100f, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
}

TEST(DebugRegistersArm64, RangeIsDifferentWatchpoint) {
  DebugRegisters regs;

  ASSERT_TRUE(Check(regs, 0x100, 1, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({0x100, 0x100 + 1}, 0), 0b00000001));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 0, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(Check(regs, 0x100, 1, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 0, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(Check(regs, 0x100, 2, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({0x100, 0x100 + 2}, 1), 0b00000011));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, 0}));

  ASSERT_TRUE(Check(regs, 0x100, 2, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, 0}));

  ASSERT_TRUE(Check(regs, 0x100, 4, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({0x100, 0x100 + 4}, 2), 0b00001111));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0x100, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 4, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, 0}));

  ASSERT_TRUE(Check(regs, 0x100, 4, debug_ipc::BreakpointType::kWrite, std::nullopt));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0x100, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 4, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, 0}));

  ASSERT_TRUE(Check(regs, 0x100, 8, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({0x100, 0x100 + 8}, 3), 0b11111111));
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0x100, 0x100}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  // Deleting is by range too.
  ASSERT_TRUE(regs.RemoveWatchpoint({0x100, 0x100 + 2}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {0x100, 0, 0x100, 0x100}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 0, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {1, 0, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, kWrite, kWrite}));

  ASSERT_FALSE(regs.RemoveWatchpoint({0x100, 0x100 + 2}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {0x100, 0, 0x100, 0x100}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 0, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {1, 0, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, kWrite, kWrite}));

  ASSERT_TRUE(regs.RemoveWatchpoint({0x100, 0x100 + 1}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, 0x100, 0x100}));
  EXPECT_TRUE(CheckEnabled(regs, {0, 0, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {0, 0, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, kWrite, kWrite}));

  ASSERT_FALSE(regs.RemoveWatchpoint({0x100, 0x100 + 1}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, 0x100, 0x100}));
  EXPECT_TRUE(CheckEnabled(regs, {0, 0, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {0, 0, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, kWrite, kWrite}));

  ASSERT_TRUE(regs.RemoveWatchpoint({0x100, 0x100 + 8}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, 0x100, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {0, 0, 1, 0}));
  ASSERT_TRUE(CheckLengths(regs, {0, 0, 4, 0}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, kWrite, 0}));

  ASSERT_FALSE(regs.RemoveWatchpoint({0x100, 0x100 + 8}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, 0x100, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {0, 0, 1, 0}));
  ASSERT_TRUE(CheckLengths(regs, {0, 0, 4, 0}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, kWrite, 0}));

  ASSERT_TRUE(regs.RemoveWatchpoint({0x100, 0x100 + 4}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {0, 0, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {0, 0, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, 0, 0}));
}

TEST(DebugRegistersArm64, DifferentTypes) {
  DebugRegisters regs;

  ASSERT_TRUE(Check(regs, kAddress1, 1, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({kAddress1, kAddress1 + 1}, 0), 0x1));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, 0, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(Check(regs, kAddress2, 2, debug_ipc::BreakpointType::kReadWrite,
                    WatchpointInfo({kAddress2, kAddress2 + 2}, 1), 0x3));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kReadWrite, 0, 0}));

  ASSERT_TRUE(Check(regs, kAddress3, 4, debug_ipc::BreakpointType::kReadWrite,
                    WatchpointInfo({kAddress3, kAddress3 + 4}, 2), 0xf));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 0}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kReadWrite, kReadWrite, 0}));

  ASSERT_TRUE(Check(regs, kAddress4, 8, debug_ipc::BreakpointType::kReadWrite,
                    WatchpointInfo({kAddress4, kAddress4 + 8}, 3), 0xff));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kReadWrite, kReadWrite, kReadWrite}));

  ASSERT_TRUE(Check(regs, kAddress5, 8, debug_ipc::BreakpointType::kWrite, std::nullopt));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress3, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kReadWrite, kReadWrite, kReadWrite}));

  ASSERT_TRUE(regs.RemoveWatchpoint({kAddress3, kAddress3 + 4}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, 0, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 0, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 0, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kReadWrite, 0, kReadWrite}));

  ASSERT_TRUE(Check(regs, kAddress5, 8, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({kAddress5, kAddress5 + 8}, 2), 0xff));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress5, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 8, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kReadWrite, kWrite, kReadWrite}));

  ASSERT_FALSE(regs.RemoveWatchpoint({kAddress3, kAddress3 + 4}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {kAddress1, kAddress2, kAddress5, kAddress4}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  EXPECT_TRUE(CheckLengths(regs, {1, 2, 8, 8}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, kReadWrite, kWrite, kReadWrite}));
}

TEST(DebugRegistersArm64, SetupRemoveWatchpoint) {
  DebugRegisters regs;

  const debug_ipc::AddressRange kRange1 = {0x100, 0x101};
  const debug_ipc::AddressRange kRange2 = {0x100, 0x102};
  const debug_ipc::AddressRange kRange3 = {0x100, 0x104};
  const debug_ipc::AddressRange kRange4 = {0x100, 0x108};
  const debug_ipc::AddressRange kRange5 = {0x100, 0x105};
  const debug_ipc::AddressRange kRange6 = {0x200, 0x201};

  auto install = regs.SetWatchpoint(debug_ipc::BreakpointType::kWrite, kRange1, kWatchpointCount);
  ASSERT_TRUE(install);
  EXPECT_EQ(install->range, kRange1);
  EXPECT_EQ(install->slot, 0);

  install = regs.SetWatchpoint(debug_ipc::BreakpointType::kWrite, kRange2, kWatchpointCount);
  ASSERT_TRUE(install);
  EXPECT_EQ(install->range, kRange2);
  EXPECT_EQ(install->slot, 1);
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0, 0}));
  ASSERT_TRUE(CheckEnabled(regs, {1, 1, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 0, 0}));
  ASSERT_TRUE(CheckTypes(regs, {kWrite, kWrite, 0, 0}));

  install = regs.SetWatchpoint(debug_ipc::BreakpointType::kWrite, kRange2, kWatchpointCount);
  ASSERT_FALSE(install);

  install = regs.SetWatchpoint(debug_ipc::BreakpointType::kWrite, kRange5, kWatchpointCount);
  ASSERT_FALSE(install);

  install = regs.SetWatchpoint(debug_ipc::BreakpointType::kWrite, kRange3, kWatchpointCount);
  ASSERT_TRUE(install);
  EXPECT_EQ(install->range, kRange3);
  EXPECT_EQ(install->slot, 2);
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0x100, 0}));
  ASSERT_TRUE(CheckEnabled(regs, {1, 1, 1, 0}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 4, 0}));
  ASSERT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, 0}));

  install = regs.SetWatchpoint(debug_ipc::BreakpointType::kWrite, kRange4, kWatchpointCount);
  ASSERT_TRUE(install);
  EXPECT_EQ(install->range, kRange4);
  EXPECT_EQ(install->slot, 3);
  ASSERT_TRUE(CheckAddresses(regs, {0x100, 0x100, 0x100, 0x100}));
  ASSERT_TRUE(CheckEnabled(regs, {1, 1, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {1, 2, 4, 8}));
  ASSERT_TRUE(CheckTypes(regs, {kWrite, kWrite, kWrite, kWrite}));

  install = regs.SetWatchpoint(debug_ipc::BreakpointType::kWrite, kRange6, kWatchpointCount);
  ASSERT_FALSE(install);

  // Removing.
  ASSERT_TRUE(regs.RemoveWatchpoint(kRange1, kWatchpointCount));
  ASSERT_TRUE(CheckAddresses(regs, {0, 0x100, 0x100, 0x100}));
  ASSERT_TRUE(CheckEnabled(regs, {0, 1, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {0, 2, 4, 8}));
  ASSERT_TRUE(CheckTypes(regs, {0, kWrite, kWrite, kWrite}));

  ASSERT_FALSE(regs.RemoveWatchpoint(kRange1, kWatchpointCount));
  ASSERT_TRUE(CheckAddresses(regs, {0, 0x100, 0x100, 0x100}));
  ASSERT_TRUE(CheckEnabled(regs, {0, 1, 1, 1}));
  ASSERT_TRUE(CheckLengths(regs, {0, 2, 4, 8}));
  ASSERT_TRUE(CheckTypes(regs, {0, kWrite, kWrite, kWrite}));

  ASSERT_TRUE(regs.RemoveWatchpoint(kRange4, kWatchpointCount));
  ASSERT_TRUE(CheckAddresses(regs, {0, 0x100, 0x100, 0}));
  ASSERT_TRUE(CheckEnabled(regs, {0, 1, 1, 0}));
  ASSERT_TRUE(CheckLengths(regs, {0, 2, 4, 0}));
  ASSERT_TRUE(CheckTypes(regs, {0, kWrite, kWrite, 0}));

  ASSERT_TRUE(regs.RemoveWatchpoint(kRange3, kWatchpointCount));
  ASSERT_TRUE(CheckAddresses(regs, {0, 0x100, 0, 0}));
  ASSERT_TRUE(CheckEnabled(regs, {0, 1, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {0, 2, 0, 0}));
  ASSERT_TRUE(CheckTypes(regs, {0, kWrite, 0, 0}));

  ASSERT_TRUE(regs.RemoveWatchpoint(kRange2, kWatchpointCount));
  ASSERT_TRUE(CheckAddresses(regs, {0, 0, 0, 0}));
  ASSERT_TRUE(CheckEnabled(regs, {0, 0, 0, 0}));
  ASSERT_TRUE(CheckLengths(regs, {0, 0, 0, 0}));
  ASSERT_TRUE(CheckTypes(regs, {0, 0, 0, 0}));
}

TEST(DebugRegistersArm64, RemoveLargeAddress) {
  constexpr uint64_t kBigAddress = 0x1'0000'0000;
  DebugRegisters regs;

  ASSERT_TRUE(Check(regs, kBigAddress, 8, debug_ipc::BreakpointType::kWrite,
                    WatchpointInfo({kBigAddress, kBigAddress + 8}, 0), 0b11111111));
  EXPECT_TRUE(CheckAddresses(regs, {kBigAddress, 0, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {1, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {8, 0, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {kWrite, 0, 0, 0}));

  ASSERT_TRUE(regs.RemoveWatchpoint({kBigAddress, kBigAddress + 8}, kWatchpointCount));
  EXPECT_TRUE(CheckAddresses(regs, {0, 0, 0, 0}));
  EXPECT_TRUE(CheckEnabled(regs, {0, 0, 0, 0}));
  EXPECT_TRUE(CheckLengths(regs, {0, 0, 0, 0}));
  EXPECT_TRUE(CheckTypes(regs, {0, 0, 0, 0}));
}

}  // namespace debug_agent
