// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_ARM64_HELPERS_UNITTEST_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_ARM64_HELPERS_UNITTEST_H_

#include <stdint.h>
#include <zircon/hw/debug/arm64.h>
#include <zircon/status.h>
#include <zircon/syscalls/debug.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch_arm64_helpers.h"

namespace debug_agent {
namespace arch {

constexpr uint32_t kWatchpointCount = 4;

inline bool ResultVerification(zx_thread_state_debug_regs_t* regs, uint64_t address, uint64_t size,
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

inline bool Check(zx_thread_state_debug_regs_t* regs, uint64_t address, uint64_t size,
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

inline bool ResetCheck(zx_thread_state_debug_regs_t* regs, uint64_t address, uint64_t size,
                       WatchpointInstallationResult expected, uint32_t expected_bas = 0) {
  // Restart the registers.
  *regs = {};
  return Check(regs, address, size, expected, expected_bas);
}

inline bool CheckAddresses(const zx_thread_state_debug_regs_t& regs,
                           std::vector<uint64_t> addresses) {
  for (uint32_t i = 0; i < addresses.size(); i++) {
    if (regs.hw_wps[i].dbgwvr != addresses[i]) {
      ADD_FAILURE() << "Reg " << i << " mismatch. Expected: 0x" << std::hex << addresses[i]
                    << ", got: " << regs.hw_wps[i].dbgwvr;
      return false;
    }
  }

  return true;
}

inline uint32_t CountBASBits(uint32_t bas) {
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

inline bool CheckLengths(const zx_thread_state_debug_regs_t& regs, std::vector<uint32_t> lengths) {
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

inline bool CheckEnabled(const zx_thread_state_debug_regs& regs, std::vector<uint32_t> enabled) {
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

inline bool CheckTypes(const zx_thread_state_debug_regs& regs, std::vector<uint32_t> types) {
  for (uint32_t i = 0; i < types.size(); i++) {
    uint32_t type = ARM64_DBGWCR_LSC_GET(regs.hw_wps[i].dbgwcr);
    if (type != types[i]) {
      ADD_FAILURE() << "Reg " << i << " wrong type. Expected: " << types[i] << ", got: " << type;
      return false;
    }
  }

  return true;
}

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_ARM64_HELPERS_UNITTEST_H_
