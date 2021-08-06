// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/hw/debug/x86.h>

#include "src/developer/debug/debug_agent/align.h"
#include "src/developer/debug/debug_agent/debug_registers.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

uint64_t HWDebugResourceEnabled(uint64_t dr7, size_t index) {
  FX_DCHECK(index < 4);

  // clang-format off
  static uint64_t masks[4] = { X86_FLAG_MASK(DR7L0),
                               X86_FLAG_MASK(DR7L1),
                               X86_FLAG_MASK(DR7L2),
                               X86_FLAG_MASK(DR7L3)};
  // clang-format on

  return (dr7 & masks[index]) != 0;
}

// A watchpoint is configured by DR7RW<i> = 0b10 (write) or 0b11 (read/write).
bool IsWatchpoint(uint64_t dr7, size_t index) {
  // clang-format off
  switch (index) {
    case 0: return (X86_FLAG_VALUE(dr7, DR7RW0) & 1) > 0;
    case 1: return (X86_FLAG_VALUE(dr7, DR7RW1) & 1) > 0;
    case 2: return (X86_FLAG_VALUE(dr7, DR7RW2) & 1) > 0;
    case 3: return (X86_FLAG_VALUE(dr7, DR7RW3) & 1) > 0;
  }
  // clang-format on

  FX_NOTREACHED();
  return false;
}

uint64_t WatchpointAddressAlign(const debug::AddressRange& range) {
  // clang-format off
  switch (range.size()) {
    case 1: return range.begin();
    case 2: return range.begin() & (uint64_t)(~0b1);
    case 4: return range.begin() & (uint64_t)(~0b11);
    case 8: return range.begin() & (uint64_t)(~0b111);
  }
  // clang-format on

  return 0;
}

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

uint64_t LengthToX86Length(uint64_t len) {
  // clang-format off
  switch (len) {
    case 1: return 0;
    case 2: return 1;
    case 8: return 2;
    case 4: return 3;
  }
  // clang-format on

  FX_NOTREACHED() << "Invalid len: " << len;
  return 0;
}

void SetWatchpointFlags(uint64_t* dr7, int slot, bool active, uint64_t size,
                        debug_ipc::BreakpointType type) {
  uint32_t rw_value = 0;
  if (type == debug_ipc::BreakpointType::kWrite) {
    rw_value = 0b1;
  } else if (type == debug_ipc::BreakpointType::kReadWrite) {
    rw_value = 0b11;
  }

  // clang-format off
  switch (slot) {
    case 0: {
      X86_DBG_CONTROL_L0_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW0_SET(dr7, rw_value);
      X86_DBG_CONTROL_LEN0_SET(dr7, size ? LengthToX86Length(size) : 0);
      return;
    }
    case 1: {
      X86_DBG_CONTROL_L1_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW1_SET(dr7, rw_value);
      X86_DBG_CONTROL_LEN1_SET(dr7, size ? LengthToX86Length(size) : 0);
      return;
    }
    case 2: {
      X86_DBG_CONTROL_L2_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW2_SET(dr7, rw_value);
      X86_DBG_CONTROL_LEN2_SET(dr7, size ? LengthToX86Length(size) : 0);
      return;
    }
    case 3: {
      X86_DBG_CONTROL_L3_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW3_SET(dr7, rw_value);
      X86_DBG_CONTROL_LEN3_SET(dr7, size ? LengthToX86Length(size) : 0);
      return;
    }
  }
  // clang-format on

  FX_NOTREACHED() << "Invalid slot: " << slot;
}

// x86 uses the following bits to represent watchpoint lenghts:
//   00: 1 byte.
//   10: 2 bytes.
//   11: 8 bytes.
//   10: 4 bytes.
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

void SetHWBreakpointFlags(uint64_t* dr7, int slot, bool active) {
  switch (slot) {
    case 0: {
      X86_DBG_CONTROL_L0_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW0_SET(dr7, 0);
      X86_DBG_CONTROL_LEN0_SET(dr7, 0);
      return;
    }
    case 1: {
      X86_DBG_CONTROL_L1_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW1_SET(dr7, 0);
      X86_DBG_CONTROL_LEN1_SET(dr7, 0);
      return;
    }
    case 2: {
      X86_DBG_CONTROL_L2_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW2_SET(dr7, 0);
      X86_DBG_CONTROL_LEN2_SET(dr7, 0);
      return;
    }
    case 3: {
      X86_DBG_CONTROL_L3_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW3_SET(dr7, 0);
      X86_DBG_CONTROL_LEN3_SET(dr7, 0);
      return;
    }
  }

  FX_NOTREACHED() << "Invalid slot: " << slot;
}

std::string DR6ToString(uint64_t dr6) {
  return fxl::StringPrintf(
      "0x%lx: B0=%d, B1=%d, B2=%d, B3=%d, BD=%d, BS=%d, BT=%d", dr6, X86_FLAG_VALUE(dr6, DR6B0),
      X86_FLAG_VALUE(dr6, DR6B1), X86_FLAG_VALUE(dr6, DR6B2), X86_FLAG_VALUE(dr6, DR6B3),
      X86_FLAG_VALUE(dr6, DR6BD), X86_FLAG_VALUE(dr6, DR6BS), X86_FLAG_VALUE(dr6, DR6BT));
}

std::string DR7ToString(uint64_t dr7) {
  return fxl::StringPrintf(
      "0x%lx: L0=%d, G0=%d, L1=%d, G1=%d, L2=%d, G2=%d, L3=%d, G4=%d, LE=%d, "
      "GE=%d, GD=%d, R/W0=%d, LEN0=%d, R/W1=%d, LEN1=%d, R/W2=%d, LEN2=%d, "
      "R/W3=%d, LEN3=%d",
      dr7, X86_FLAG_VALUE(dr7, DR7L0), X86_FLAG_VALUE(dr7, DR7G0), X86_FLAG_VALUE(dr7, DR7L1),
      X86_FLAG_VALUE(dr7, DR7G1), X86_FLAG_VALUE(dr7, DR7L2), X86_FLAG_VALUE(dr7, DR7G2),
      X86_FLAG_VALUE(dr7, DR7L3), X86_FLAG_VALUE(dr7, DR7G3), X86_FLAG_VALUE(dr7, DR7LE),
      X86_FLAG_VALUE(dr7, DR7GE), X86_FLAG_VALUE(dr7, DR7GD), X86_FLAG_VALUE(dr7, DR7RW0),
      X86_FLAG_VALUE(dr7, DR7LEN0), X86_FLAG_VALUE(dr7, DR7RW1), X86_FLAG_VALUE(dr7, DR7LEN1),
      X86_FLAG_VALUE(dr7, DR7RW2), X86_FLAG_VALUE(dr7, DR7LEN2), X86_FLAG_VALUE(dr7, DR7RW3),
      X86_FLAG_VALUE(dr7, DR7LEN3));
}

}  // namespace

bool DebugRegisters::SetHWBreakpoint(uint64_t address) {
  // Search for a free slot.
  int slot = -1;
  for (size_t i = 0; i < 4; i++) {
    if (HWDebugResourceEnabled(regs_.dr7, i)) {
      // If it's already bound there, we don't need to do anything.
      if (regs_.dr[i] == address)
        return false;
    } else {
      slot = i;
      break;
    }
  }

  if (slot == -1)
    return false;

  // We found a slot, we bind the address.
  regs_.dr[slot] = address;
  SetHWBreakpointFlags(&regs_.dr7, slot, true);
  return true;
}

bool DebugRegisters::RemoveHWBreakpoint(uint64_t address) {
  // Search for the slot.
  for (int i = 0; i < 4; i++) {
    if (!HWDebugResourceEnabled(regs_.dr7, i) || IsWatchpoint(regs_.dr7, i))
      continue;

    if (regs_.dr[i] != address)
      continue;

    // Clear this breakpoint.
    regs_.dr[i] = 0;
    SetHWBreakpointFlags(&regs_.dr7, i, false);

    return true;
  }

  // We didn't find the address.
  return false;
}

// On x64, watchpoint_count is unnecessary for this computation.
std::optional<WatchpointInfo> DebugRegisters::SetWatchpoint(debug_ipc::BreakpointType type,
                                                            const debug::AddressRange& range,
                                                            uint32_t watchpoint_count) {
  if (!debug_ipc::IsWatchpointType(type))
    return std::nullopt;

  // Create an aligned range that will cover the watchpoint.
  auto aligned_range = AlignRange(range);
  if (!aligned_range.has_value())
    return std::nullopt;

  uint64_t address = aligned_range->begin();
  uint64_t size = aligned_range->end() - aligned_range->begin();

  // Search for a free slot.
  int slot = -1;
  for (int i = 0; i < 4; i++) {
    if (HWDebugResourceEnabled(regs_.dr7, i)) {
      // If it's the same address, we don't need to do anything.
      // For watchpoints, we need to compare against the range.
      if ((regs_.dr[i] == address) && GetWatchpointLength(regs_.dr7, i) == size)
        return std::nullopt;
    } else {
      slot = i;
      break;
    }
  }

  if (slot == -1)
    return std::nullopt;

  // We found a slot, we bind the watchpoint.
  regs_.dr[slot] = address;
  SetWatchpointFlags(&regs_.dr7, slot, true, size, type);

  return WatchpointInfo(aligned_range.value(), slot);
}

// On x64, watchpoint_count is unnecessary for this computation.
bool DebugRegisters::RemoveWatchpoint(const debug::AddressRange& range, uint32_t watchpoint_count) {
  uint64_t aligned_address = WatchpointAddressAlign(range);
  if (!aligned_address)
    return false;

  for (int slot = 0; slot < 4; slot++) {
    if (!IsWatchpoint(regs_.dr7, slot))
      continue;

    // Both address and length should match.
    if ((regs_.dr[slot] != aligned_address) ||
        GetWatchpointLength(regs_.dr7, slot) != range.size()) {
      continue;
    }

    // Clear this breakpoint.
    regs_.dr[slot] = 0;
    SetWatchpointFlags(&regs_.dr7, slot, false, 0, debug_ipc::BreakpointType::kLast);
    return true;
  }

  // We didn't find the address.
  return false;
}

std::optional<WatchpointInfo> DebugRegisters::DecodeHitWatchpoint() const {
  uint64_t addr = 0;
  uint64_t length = 0;
  int slot = 0;
  if (X86_FLAG_VALUE(regs_.dr6, DR6B0)) {
    addr = regs_.dr[0];
    length = GetWatchpointLength(regs_.dr7, 0);
    slot = 0;
  } else if (X86_FLAG_VALUE(regs_.dr6, DR6B1)) {
    addr = regs_.dr[1];
    length = GetWatchpointLength(regs_.dr7, 1);
    slot = 1;
  } else if (X86_FLAG_VALUE(regs_.dr6, DR6B2)) {
    addr = regs_.dr[2];
    length = GetWatchpointLength(regs_.dr7, 2);
    slot = 2;
  } else if (X86_FLAG_VALUE(regs_.dr6, DR6B3)) {
    addr = regs_.dr[3];
    length = GetWatchpointLength(regs_.dr7, 3);
    slot = 3;
  } else {
    FX_NOTREACHED() << "x86: No known hw exception set in DR6";
    return std::nullopt;
  }

  return WatchpointInfo({addr, addr + length}, slot);
}

void DebugRegisters::SetForHitWatchpoint(int slot) {
  switch (slot) {
    case 0:
      regs_.dr6 |= X86_FLAG_MASK(DR6B0);
      break;
    case 1:
      regs_.dr6 |= X86_FLAG_MASK(DR6B1);
      break;
    case 2:
      regs_.dr6 |= X86_FLAG_MASK(DR6B2);
      break;
    case 3:
      regs_.dr6 |= X86_FLAG_MASK(DR6B3);
      break;
    default:
      FX_NOTREACHED() << "Bad slot.";
      break;
  }
}

std::string DebugRegisters::ToString() const {
  std::stringstream ss;
  ss << "Regs: " << std::endl
     << "DR0: 0x" << std::hex << regs_.dr[0] << std::endl
     << "DR1: 0x" << std::hex << regs_.dr[1] << std::endl
     << "DR2: 0x" << std::hex << regs_.dr[2] << std::endl
     << "DR3: 0x" << std::hex << regs_.dr[3] << std::endl
     << "DR6: " << DR6ToString(regs_.dr6) << std::endl
     << "DR7: " << DR7ToString(regs_.dr7) << std::endl;
  return ss.str();
}

}  // namespace debug_agent
