// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/hw/debug/arm64.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debug_registers.h"
#include "src/developer/debug/shared/arch_arm64.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

// Validates that the range is properly aligned and masked. Returns 0 on failure.
uint64_t ValidateRange(const debug_ipc::AddressRange& range) {
  constexpr uint64_t kMask = 0b11;

  if (range.size() == 1) {
    return range.begin() & ~kMask;
  } else if (range.size() == 2) {
    // Should be 2-byte aligned.
    if ((range.begin() & 0b1) != 0)
      return 0;
    return range.begin() & ~kMask;
  } else if (range.size() == 4) {
    // Should be 4-byte aligned.
    if ((range.begin() & 0b11) != 0)
      return 0;
    return range.begin() & ~kMask;
  } else if (range.size() == 8) {
    // Should be 8-byte aligned.
    if ((range.begin() & 0b111) != 0)
      return 0;
    return range.begin() & ~kMask;
  } else {
    return 0;
  }
}

// Returns the bit flag to enable each different kind of watchpoint.
uint32_t GetWatchpointWriteFlag(debug_ipc::BreakpointType type) {
  // clang-format off
  switch (type) {
    case debug_ipc::BreakpointType::kReadWrite: return 0b11;
    case debug_ipc::BreakpointType::kWrite: return 0b10;
    case debug_ipc::BreakpointType::kSoftware:
    case debug_ipc::BreakpointType::kHardware:
    case debug_ipc::BreakpointType::kLast:
      break;
  }
  // clang-format on

  FX_NOTREACHED() << "Invalid breakpoint type: " << static_cast<uint32_t>(type);
  return 0;
}

void SetWatchpointFlags(uint32_t* dbgwcr, debug_ipc::BreakpointType type, uint64_t base_address,
                        const debug_ipc::AddressRange& range) {
  if (range.size() == 1) {
    uint32_t bas = 1u << (range.begin() - base_address);
    ARM64_DBGWCR_BAS_SET(dbgwcr, bas);
  } else if (range.size() == 2) {
    uint32_t bas = 0b11 << (range.begin() - base_address);
    ARM64_DBGWCR_BAS_SET(dbgwcr, bas);
  } else if (range.size() == 4) {
    uint32_t bas = 0b1111 << (range.begin() - base_address);
    ARM64_DBGWCR_BAS_SET(dbgwcr, bas);
  } else if (range.size() == 8) {
    ARM64_DBGWCR_BAS_SET(dbgwcr, 0xff);
  } else {
    FX_NOTREACHED() << "Invalid range size: " << range.size();
  }

  // Set type.
  ARM64_DBGWCR_LSC_SET(dbgwcr, GetWatchpointWriteFlag(type));

  // Set enabled.
  ARM64_DBGWCR_E_SET(dbgwcr, 1);
}

uint32_t GetWatchpointLength(uint32_t dbgwcr) {
  // Because base range addresses have to be 4 bytes aligned, having a watchpoint for smaller ranges
  // (1, 2 or 4 bytes) could have many combinarions of the BAS register (which determines which
  // byte will trigger an exception offseted from the base range address.

  // clang-format off
  uint32_t bas = ARM64_DBGWCR_BAS_GET(dbgwcr);
  switch (bas) {
    case 0b00000000: return 0;

    case 0b00000001: return 1;
    case 0b00000010: return 1;
    case 0b00000100: return 1;
    case 0b00001000: return 1;
    case 0b00010000: return 1;
    case 0b00100000: return 1;
    case 0b01000000: return 1;
    case 0b10000000: return 1;

    case 0b00000011: return 2;
    case 0b00001100: return 2;
    case 0b00110000: return 2;
    case 0b11000000: return 2;

    case 0b00001111: return 4;
    case 0b11110000: return 4;

    case 0b11111111: return 8;
    default:
      FX_NOTREACHED() << "Wrong bas value: 0x" << std::hex << bas;
      return 0;
  }
  // clang-format on
}

}  // namespace

bool DebugRegisters::SetHWBreakpoint(uint64_t address) {
  // We search for an unset register.
  int slot = -1;
  for (int i = 0; i < regs_.hw_bps_count; i++) {
    auto& hw_bp = regs_.hw_bps[i];

    // If we found the same address, we don't care if it's disabled, we simply
    // mark this as our slot.
    // If the address is 0, this is an empty slot too.
    if ((hw_bp.dbgbvr == address) || (hw_bp.dbgbvr == 0)) {
      slot = i;
      break;
    }

    // If it's active this is not an empty slot.
    if ((hw_bp.dbgbcr & 1u) == 1)
      continue;

    slot = i;
    break;
  }

  if (slot == -1)
    return false;

  regs_.hw_bps[slot].dbgbcr |= 1u;
  regs_.hw_bps[slot].dbgbvr = address;
  return true;
}

bool DebugRegisters::RemoveHWBreakpoint(uint64_t address) {
  // Search for an breakpoint with this address.
  int slot = -1;
  for (int i = 0; i < regs_.hw_bps_count; i++) {
    auto& hw_bp = regs_.hw_bps[i];
    if (hw_bp.dbgbvr == address) {
      slot = i;
      break;
    }
  }

  if (slot == -1)
    return false;

  regs_.hw_bps[slot].dbgbcr = 0;
  regs_.hw_bps[slot].dbgbvr = 0;
  return true;
}

std::optional<WatchpointInfo> DebugRegisters::SetWatchpoint(debug_ipc::BreakpointType type,
                                                            const debug_ipc::AddressRange& range,
                                                            uint32_t watchpoint_count) {
  FX_DCHECK(watchpoint_count <= 16);
  if (!IsWatchpointType(type)) {
    FX_NOTREACHED() << "Requires a watchpoint type, received "
                    << debug_ipc::BreakpointTypeToString(type);
    return std::nullopt;
  }

  uint64_t base_address = ValidateRange(range);
  if (base_address == 0) {
    DEBUG_LOG(ArchArm64) << "Range is not valid for added watchpoint: " << range.ToString();
    return std::nullopt;
  }

  // Search for a free slot.
  int slot = -1;
  for (uint32_t i = 0; i < watchpoint_count; i++) {
    if (regs_.hw_wps[i].dbgwvr == 0) {
      slot = i;
      break;
    }

    // If it's the same address, we need to compare length.
    uint32_t length = GetWatchpointLength(regs_.hw_wps[i].dbgwcr);
    if (regs_.hw_wps[i].dbgwvr == base_address && length == range.size()) {
      DEBUG_LOG(ArchArm64) << "Breakpoint range already exists: " << range.ToString();
      return std::nullopt;
    }
  }

  if (slot == -1) {
    DEBUG_LOG(ArchArm64) << "No more hardware breakpoints. Not adding new one.";
    return std::nullopt;
  }

  // We found a slot, we bind the watchpoint.
  regs_.hw_wps[slot].dbgwvr = base_address;
  SetWatchpointFlags(&regs_.hw_wps[slot].dbgwcr, type, base_address, range);

  return WatchpointInfo(range, slot);
}

bool DebugRegisters::RemoveWatchpoint(const debug_ipc::AddressRange& range,
                                      uint32_t watchpoint_count) {
  FX_DCHECK(watchpoint_count <= 16);

  uint64_t base_address = ValidateRange(range);
  if (base_address == 0) {
    DEBUG_LOG(ArchArm64) << "Range is not valid for removed watchpoint: " << range.ToString();
    return false;
  }

  // Search for a slot that matches.
  int slot = -1;
  for (uint32_t i = 0; i < watchpoint_count; i++) {
    if (regs_.hw_wps[i].dbgwvr == 0)
      continue;

    // If it's the same address, we need to compare length.
    uint32_t length = GetWatchpointLength(regs_.hw_wps[i].dbgwcr);
    if (regs_.hw_wps[i].dbgwvr == base_address && length == range.size()) {
      slot = i;
      break;
    }
  }

  if (slot == -1) {
    DEBUG_LOG(ArchArm64) << "Range is not found for removed watchpoint: " << range.ToString();
    return false;
  }

  // Clear the slot.
  regs_.hw_wps[slot].dbgwcr = 0;
  regs_.hw_wps[slot].dbgwvr = 0;

  return true;
}

std::optional<WatchpointInfo> DebugRegisters::DecodeHitWatchpoint() const {
  DEBUG_LOG(ArchArm64) << "Got FAR: 0x" << std::hex << regs_.far;

  // Get the closest watchpoint.
  uint64_t min_distance = UINT64_MAX;
  int closest_index = -1;
  debug_ipc::AddressRange closest_range = {};
  for (uint32_t i = 0; i < arch::GetHardwareWatchpointCount(); i++) {
    uint64_t dbgwcr = regs_.hw_wps[i].dbgwcr;
    uint64_t dbgwvr = regs_.hw_wps[i].dbgwvr;  // The actual watchpoint address.

    DEBUG_LOG(ArchArm64) << "DBGWCR " << i << ": 0x" << std::hex << dbgwcr;

    if (!ARM64_DBGWCR_E_GET(dbgwcr))
      continue;

    uint32_t length = GetWatchpointLength(dbgwcr);
    if (length == 0)
      continue;

    const debug_ipc::AddressRange wp_range = {dbgwvr, dbgwvr + length};
    if (wp_range.InRange(regs_.far))
      return WatchpointInfo(wp_range, i);

    // Otherwise find the distance and then decide on the closest one.
    uint64_t distance = UINT64_MAX;
    if (regs_.far < wp_range.begin()) {
      distance = wp_range.begin() - regs_.far;
    } else if (regs_.far >= wp_range.end()) {
      distance = regs_.far - wp_range.end();
    } else {
      FX_NOTREACHED() << "Invalid far/range combo. FAR: 0x" << std::hex << regs_.far
                      << ", range: " << wp_range.begin() << ", " << wp_range.end();
    }

    if (distance < min_distance) {
      min_distance = distance;
      closest_index = i;
      closest_range = wp_range;
    }
  }

  return WatchpointInfo(closest_range, closest_index);
}

void DebugRegisters::SetForHitWatchpoint(int slot) {
  // ARM64 breakpoint status is nto communicated in registers so there's nothing to do.
}

std::string DebugRegisters::ToString() const {
  std::stringstream ss;

  ss << "ESR: 0x" << std::hex << regs_.esr << std::endl;

  ss << "HW breakpoints: " << std::endl;
  for (size_t i = 0; i < std::size(regs_.hw_bps); i++) {
    uint32_t dbgbcr = regs_.hw_bps[i].dbgbcr;
    uint64_t dbgbvr = regs_.hw_bps[i].dbgbvr;
    if (dbgbvr == 0)
      continue;

    ss << fxl::StringPrintf(
        "%02lu. DBGBVR: 0x%lx, DBGBCR: E=%d, PMC=%d, BAS=%d, HMC=%d, SSC=%d, LBN=%d, BT=%d", i,
        dbgbvr, ARM64_FLAG_VALUE(dbgbcr, DBGBCR, E), ARM64_FLAG_VALUE(dbgbcr, DBGBCR, PMC),
        ARM64_FLAG_VALUE(dbgbcr, DBGBCR, BAS), ARM64_FLAG_VALUE(dbgbcr, DBGBCR, HMC),
        ARM64_FLAG_VALUE(dbgbcr, DBGBCR, SSC), ARM64_FLAG_VALUE(dbgbcr, DBGBCR, LBN),
        ARM64_FLAG_VALUE(dbgbcr, DBGBCR, BT));
    ss << std::endl;
  }

  ss << "Watchpoints: " << std::endl;
  for (size_t i = 0; i < std::size(regs_.hw_wps); i++) {
    uint32_t dbgwcr = regs_.hw_wps[i].dbgwcr;
    uint64_t dbgwvr = regs_.hw_wps[i].dbgwvr;
    if (dbgwvr == 0)
      continue;

    ss << fxl::StringPrintf(
              "%02lu. DBGWVR: 0x%lx, DBGWCR: "
              "E=%d, PAC=%d, LSC=%d, BAS=0x%x, HMC=%d, SSC=%d, LBN=%d, WT=%d, MASK=0x%x",
              i, dbgwvr, ARM64_DBGWCR_E_GET(dbgwcr), ARM64_DBGWCR_PAC_GET(dbgwcr),
              ARM64_DBGWCR_LSC_GET(dbgwcr), ARM64_DBGWCR_BAS_GET(dbgwcr),
              ARM64_DBGWCR_HMC_GET(dbgwcr), ARM64_DBGWCR_SSC_GET(dbgwcr),
              ARM64_DBGWCR_LBN_GET(dbgwcr), ARM64_DBGWCR_WT_GET(dbgwcr),
              ARM64_DBGWCR_MSK_GET(dbgwcr))
       << std::endl;
  }

  return ss.str();
}

}  // namespace debug_agent
