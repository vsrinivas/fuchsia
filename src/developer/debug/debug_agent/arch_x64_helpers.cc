// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_x64_helpers.h"

#include <vector>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

using namespace debug_ipc;

namespace debug_agent {
namespace arch {

namespace {

uint64_t HWDebugResourceEnabled(uint64_t dr7, size_t index) {
  FXL_DCHECK(index < 4);
  static uint64_t masks[4] = {
      X86_FLAG_MASK(DR7L0),
      X86_FLAG_MASK(DR7L1),
      X86_FLAG_MASK(DR7L2),
      X86_FLAG_MASK(DR7L3)
  };

  return (dr7 & masks[index]) != 0;
}

// A HW breakpoint is configured by DR7RW<i> = 0b00.
bool IsHWBreakpoint(uint64_t dr7, size_t index) {
  FXL_DCHECK(index < 4);
  switch (index) {
    case 0:
      return X86_FLAG_VALUE(dr7, DR7RW0) == 0;
    case 1:
      return X86_FLAG_VALUE(dr7, DR7RW1) == 0;
    case 2:
      return X86_FLAG_VALUE(dr7, DR7RW2) == 0;
    case 3:
      return X86_FLAG_VALUE(dr7, DR7RW3) == 0;
    default:
      break;
  }

  FXL_NOTREACHED();
  return false;
}

// A watchpoint is configured by DR7RW<i> = 0b10 (write) or 0b11 (read/write).
bool IsWatchpoint(uint64_t dr7, size_t index) {
  FXL_DCHECK(index < 4);
  switch (index) {
    case 0:
      return (X86_FLAG_VALUE(dr7, DR7RW0) & 1) == 1;
    case 1:
      return (X86_FLAG_VALUE(dr7, DR7RW1) & 1) == 1;
    case 2:
      return (X86_FLAG_VALUE(dr7, DR7RW2) & 1) == 1;
    case 3:
      return (X86_FLAG_VALUE(dr7, DR7RW3) & 1) == 1;
    default:
      break;
  }

  FXL_NOTREACHED();
  return false;
}

// Mask needed to clear a particular HW debug resource.
uint64_t HWDebugResourceD7ClearMask(size_t index) {
  FXL_DCHECK(index < 4);
  static uint64_t masks[4] = {
    ~(X86_FLAG_MASK(DR7L0) | X86_FLAG_MASK(DR7RW0) | X86_FLAG_MASK(DR7LEN0)),
    ~(X86_FLAG_MASK(DR7L1) | X86_FLAG_MASK(DR7RW1) | X86_FLAG_MASK(DR7LEN1)),
    ~(X86_FLAG_MASK(DR7L2) | X86_FLAG_MASK(DR7RW2) | X86_FLAG_MASK(DR7LEN2)),
    ~(X86_FLAG_MASK(DR7L3) | X86_FLAG_MASK(DR7RW3) | X86_FLAG_MASK(DR7LEN3)),
  };
  return masks[index];
}

// Mask needed to set a particular HW breakpoint.
uint64_t HWBreakpointDR7SetMask(size_t index) {
  FXL_DCHECK(index < 4);
  // Mask is: L = 1, RW = 00, LEN = 10
  static uint64_t masks[4] = {
      X86_FLAG_MASK(DR7L0),
      X86_FLAG_MASK(DR7L1),
      X86_FLAG_MASK(DR7L2),
      X86_FLAG_MASK(DR7L3),
  };
  return masks[index];
}

uint64_t WatchpointDR7SetMask(size_t index) {
  FXL_DCHECK(index < 4);
  // Mask is: L = 1, RW = 0b01, LEN = 10 (8 bytes).
  // TODO(donosoc): This is only setting write-only watchpoints.
  //                When enabled in the client, we need to allow read/write.
  static uint64_t masks[4] = {
    X86_FLAG_MASK(DR7L0) | 0b01 << kDR7RW0Shift | 0b10 << kDR7LEN0Shift,
    X86_FLAG_MASK(DR7L1) | 0b01 << kDR7RW1Shift | 0b10 << kDR7LEN1Shift,
    X86_FLAG_MASK(DR7L2) | 0b01 << kDR7RW2Shift | 0b10 << kDR7LEN2Shift,
    X86_FLAG_MASK(DR7L3) | 0b01 << kDR7RW3Shift | 0b10 << kDR7LEN3Shift,
  };
  return masks[index];
}

}  // namespace

zx_status_t WriteGeneralRegisters(const std::vector<debug_ipc::Register>& regs,
                                  zx_thread_state_general_regs_t* gen_regs) {
  uint32_t begin = static_cast<uint32_t>(debug_ipc::RegisterID::kX64_rax);
  uint32_t end = static_cast<uint32_t>(debug_ipc::RegisterID::kX64_rflags);
  for (const debug_ipc::Register& reg : regs) {
    if (reg.data.size() != 8)
      return ZX_ERR_INVALID_ARGS;

    // zx_thread_state_general_regs has the same layout as the RegisterID enum
    // for x64 general registers.
    uint32_t id = static_cast<uint32_t>(reg.id);
    if (id > end)
      return ZX_ERR_INVALID_ARGS;

    // Insert the value to the correct offset.
    uint32_t offset = id - begin;
    uint64_t* reg_ptr = reinterpret_cast<uint64_t*>(gen_regs);
    reg_ptr += offset;
    *reg_ptr = *reinterpret_cast<const uint64_t*>(reg.data.data());
  }

  return ZX_OK;
}

// HW Breakpoints --------------------------------------------------------------

zx_status_t SetupHWBreakpoint(uint64_t address,
                              zx_thread_state_debug_regs_t* debug_regs) {
  // Search for a free slot.
  int slot = -1;
  for (size_t i = 0; i < 4; i++) {
    if (HWDebugResourceEnabled(debug_regs->dr7, i)) {
      // If it's already bound there, we don't need to do anything.
      if (debug_regs->dr[i] == address)
        return ZX_ERR_ALREADY_BOUND;
    } else {
      slot = i;
      break;
    }
  }

  if (slot == -1)
    return ZX_ERR_NO_RESOURCES;

  // We found a slot, we bind the address.
  debug_regs->dr[slot] = address;
  debug_regs->dr7 &= HWDebugResourceD7ClearMask(slot);
  debug_regs->dr7 |= HWBreakpointDR7SetMask(slot);
  return ZX_OK;
}

zx_status_t RemoveHWBreakpoint(uint64_t address,
                               zx_thread_state_debug_regs_t* debug_regs) {
  // Search for the slot.
  for (int i = 0; i < 4; i++) {
    if (!HWDebugResourceEnabled(debug_regs->dr7, i) ||
        IsWatchpoint(debug_regs->dr7, i)) {
      continue;
    }

    if (debug_regs->dr[i] != address)
      continue;

    // Clear this breakpoint.
    debug_regs->dr[i] = 0;
    debug_regs->dr7 &= HWDebugResourceD7ClearMask(i);
    return ZX_OK;
  }

  // We didn't find the address.
  return ZX_ERR_OUT_OF_RANGE;
}

// HW Watchpoints --------------------------------------------------------------

namespace {

inline uint64_t AlignedAddress(uint64_t address) {
  return address & ~0b111;
}

}

zx_status_t SetupWatchpoint(uint64_t address,
                            zx_thread_state_debug_regs_t* debug_regs) {
  // Search for a free slot.
  int slot = -1;
  for (int i = 0; i < 4; i++) {
    if (HWDebugResourceEnabled(debug_regs->dr7, i)) {
      // If it's the same address, we don't need to do anything.
      // For watchpoints, we need to compare against the aligned address.
      if (debug_regs->dr[i] == AlignedAddress(address))
        return ZX_ERR_ALREADY_BOUND;
    } else {
      slot = i;
      break;
    }
  }

  if (slot == -1)
    return ZX_ERR_NO_RESOURCES;

  // We found a slot, we bind the watchpoint.
  debug_regs->dr[slot] = AlignedAddress(address);   // 8-byte aligned.
  debug_regs->dr7 &= HWDebugResourceD7ClearMask(slot);
  debug_regs->dr7 |= WatchpointDR7SetMask(slot);
  return ZX_OK;
}

zx_status_t RemoveWatchpoint(uint64_t address,
                             zx_thread_state_debug_regs_t* debug_regs) {
  for (int i = 0; i < 4; i++) {
    if (!HWDebugResourceEnabled(debug_regs->dr7, i) ||
        IsHWBreakpoint(debug_regs->dr7, i)) {
      continue;
    }

    if (debug_regs->dr[i] != AlignedAddress(address))
      continue;

    // Clear this breakpoint.
    debug_regs->dr[i] = 0;
    debug_regs->dr7 &= HWDebugResourceD7ClearMask(i);
    return ZX_OK;
  }

  // We didn't find the address.
  return ZX_ERR_OUT_OF_RANGE;
}

// Debug functions -------------------------------------------------------------

std::string GeneralRegistersToString(const zx_thread_state_general_regs& regs) {
  std::stringstream ss;
  ss << "General regs: " << std::endl
     << "rax: 0x" << std::hex << regs.rax << std::endl
     << "rbx: 0x" << std::hex << regs.rbx << std::endl
     << "rcx: 0x" << std::hex << regs.rcx << std::endl
     << "rdx: 0x" << std::hex << regs.rdx << std::endl
     << "rsi: 0x" << std::hex << regs.rsi << std::endl
     << "rdi: 0x" << std::hex << regs.rdi << std::endl
     << "rbp: 0x" << std::hex << regs.rbp << std::endl
     << "rsp: 0x" << std::hex << regs.rsp << std::endl
     << "r8: 0x" << std::hex << regs.r8 << std::endl
     << "r9: 0x" << std::hex << regs.r9 << std::endl
     << "r10: 0x" << std::hex << regs.r10 << std::endl
     << "r11: 0x" << std::hex << regs.r11 << std::endl
     << "r12: 0x" << std::hex << regs.r12 << std::endl
     << "r13: 0x" << std::hex << regs.r13 << std::endl
     << "r14: 0x" << std::hex << regs.r14 << std::endl
     << "r15: 0x" << std::hex << regs.r15 << std::endl
     << "rip: 0x" << std::hex << regs.rip << std::endl
     << "rflags: 0x" << std::hex << regs.rflags;

  return ss.str();
}

std::string DebugRegistersToString(const zx_thread_state_debug_regs_t& regs) {
  std::stringstream ss;
  ss << "Regs: " << std::endl
     << "DR0: 0x" << std::hex << regs.dr[0] << std::endl
     << "DR1: 0x" << std::hex << regs.dr[1] << std::endl
     << "DR2: 0x" << std::hex << regs.dr[2] << std::endl
     << "DR3: 0x" << std::hex << regs.dr[3] << std::endl
     << "DR6: " << DR6ToString(regs.dr6) << std::endl
     << "DR7: " << DR7ToString(regs.dr7) << std::endl;

  return ss.str();
}

std::string DR6ToString(uint64_t dr6) {
  return fxl::StringPrintf(
      "0x%lx: B0=%d, B1=%d, B2=%d, B3=%d, BD=%d, BS=%d, BT=%d", dr6,
      X86_FLAG_VALUE(dr6, DR6B0), X86_FLAG_VALUE(dr6, DR6B1),
      X86_FLAG_VALUE(dr6, DR6B2), X86_FLAG_VALUE(dr6, DR6B3),
      X86_FLAG_VALUE(dr6, DR6BD), X86_FLAG_VALUE(dr6, DR6BS),
      X86_FLAG_VALUE(dr6, DR6BT));
}

std::string DR7ToString(uint64_t dr7) {
  return fxl::StringPrintf(
      "0x%lx: L0=%d, G0=%d, L1=%d, G1=%d, L2=%d, G2=%d, L3=%d, G4=%d, LE=%d, "
      "GE=%d, GD=%d, R/W0=%d, LEN0=%d, R/W1=%d, LEN1=%d, R/W2=%d, LEN2=%d, "
      "R/W3=%d, LEN3=%d",
      dr7, X86_FLAG_VALUE(dr7, DR7L0), X86_FLAG_VALUE(dr7, DR7G0),
      X86_FLAG_VALUE(dr7, DR7L1), X86_FLAG_VALUE(dr7, DR7G1),
      X86_FLAG_VALUE(dr7, DR7L2), X86_FLAG_VALUE(dr7, DR7G2),
      X86_FLAG_VALUE(dr7, DR7L3), X86_FLAG_VALUE(dr7, DR7G3),
      X86_FLAG_VALUE(dr7, DR7LE), X86_FLAG_VALUE(dr7, DR7GE),
      X86_FLAG_VALUE(dr7, DR7GD), X86_FLAG_VALUE(dr7, DR7RW0),
      X86_FLAG_VALUE(dr7, DR7LEN0), X86_FLAG_VALUE(dr7, DR7RW1),
      X86_FLAG_VALUE(dr7, DR7LEN1), X86_FLAG_VALUE(dr7, DR7RW2),
      X86_FLAG_VALUE(dr7, DR7LEN2), X86_FLAG_VALUE(dr7, DR7RW3),
      X86_FLAG_VALUE(dr7, DR7LEN3));
}

}  // namespace arch
}  // namespace debug_agent
