// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_x64_helpers.h"

#include <zircon/hw/debug/x86.h>

#include <vector>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/arch_helpers.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

using namespace debug_ipc;

namespace debug_agent {
namespace arch {

namespace {

// Implements a case statement for calling WriteRegisterValue assuming the Zircon register
// field matches the enum name. This avoids implementation typos where the names don't match.
#define IMPLEMENT_CASE_WRITE_REGISTER_VALUE(name)  \
  case RegisterID::kX64_##name:                    \
    status = WriteRegisterValue(reg, &regs->name); \
    break;

uint64_t HWDebugResourceEnabled(uint64_t dr7, size_t index) {
  FXL_DCHECK(index < 4);
  static uint64_t masks[4] = {X86_FLAG_MASK(DR7L0), X86_FLAG_MASK(DR7L1), X86_FLAG_MASK(DR7L2),
                              X86_FLAG_MASK(DR7L3)};

  return (dr7 & masks[index]) != 0;
}

// A watchpoint is configured by DR7RW<i> = 0b10 (write) or 0b11 (read/write).
bool IsWatchpoint(uint64_t dr7, size_t index) {
  FXL_DCHECK(index < 4);
  switch (index) {
    case 0:
      return (X86_FLAG_VALUE(dr7, DR7RW0) & 1) > 0;
    case 1:
      return (X86_FLAG_VALUE(dr7, DR7RW1) & 1) > 0;
    case 2:
      return (X86_FLAG_VALUE(dr7, DR7RW2) & 1) > 0;
    case 3:
      return (X86_FLAG_VALUE(dr7, DR7RW3) & 1) > 0;
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

}  // namespace

zx_status_t WriteGeneralRegisters(const std::vector<Register>& updates,
                                  zx_thread_state_general_regs_t* regs) {
  uint32_t begin = static_cast<uint32_t>(RegisterID::kX64_rax);
  uint32_t last = static_cast<uint32_t>(RegisterID::kX64_rflags);

  uint64_t* output_array = reinterpret_cast<uint64_t*>(regs);

  for (const Register& reg : updates) {
    if (reg.data.size() != 8)
      return ZX_ERR_INVALID_ARGS;

    // zx_thread_state_general_regs has the same layout as the RegisterID enum for x64 general
    // registers.
    uint32_t id = static_cast<uint32_t>(reg.id);
    if (id < begin || id > last)
      return ZX_ERR_INVALID_ARGS;

    // Insert the value to the correct offset.
    output_array[id - begin] = *reinterpret_cast<const uint64_t*>(reg.data.data());
  }

  return ZX_OK;
}

zx_status_t WriteFloatingPointRegisters(const std::vector<Register>& updates,
                                        zx_thread_state_fp_regs_t* regs) {
  for (const auto& reg : updates) {
    zx_status_t status = ZX_OK;
    if (reg.id >= RegisterID::kX64_st0 && reg.id <= RegisterID::kX64_st7) {
      // FP stack value.
      uint32_t stack_index =
          static_cast<uint32_t>(reg.id) - static_cast<uint32_t>(RegisterID::kX64_st0);
      status = WriteRegisterValue(reg, &regs->st[stack_index]);
    } else {
      // FP control registers.
      switch (reg.id) {
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(fcw);
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(fsw);
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(ftw);
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(fop);
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(fip);
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(fdp);
        default:
          status = ZX_ERR_INVALID_ARGS;
          break;
      }
    }

    if (status != ZX_OK)
      return status;
  }
  return ZX_OK;
}

zx_status_t WriteVectorRegisters(const std::vector<Register>& updates,
                                 zx_thread_state_vector_regs_t* regs) {
  for (const auto& reg : updates) {
    zx_status_t status = ZX_OK;
    if (static_cast<uint32_t>(reg.id) >= static_cast<uint32_t>(RegisterID::kX64_zmm0) &&
        static_cast<uint32_t>(reg.id) <= static_cast<uint32_t>(RegisterID::kX64_zmm31)) {
      uint32_t stack_index =
          static_cast<uint32_t>(reg.id) - static_cast<uint32_t>(RegisterID::kX64_zmm0);
      status = WriteRegisterValue(reg, &regs->zmm[stack_index]);
    } else {
      switch (reg.id) {
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(mxcsr);
        default:
          status = ZX_ERR_INVALID_ARGS;
          break;
      }
      if (status != ZX_OK)
        return status;
    }
  }
  return ZX_OK;
}

zx_status_t WriteDebugRegisters(const std::vector<Register>& updates,
                                zx_thread_state_debug_regs_t* regs) {
  for (const auto& reg : updates) {
    zx_status_t status = ZX_OK;
    switch (reg.id) {
      case RegisterID::kX64_dr0:
        status = WriteRegisterValue(reg, &regs->dr[0]);
        break;
      case RegisterID::kX64_dr1:
        status = WriteRegisterValue(reg, &regs->dr[1]);
        break;
      case RegisterID::kX64_dr2:
        status = WriteRegisterValue(reg, &regs->dr[2]);
        break;
      case RegisterID::kX64_dr3:
        status = WriteRegisterValue(reg, &regs->dr[3]);
        break;
      case RegisterID::kX64_dr6:
        status = WriteRegisterValue(reg, &regs->dr6);
        break;
      case RegisterID::kX64_dr7:
        status = WriteRegisterValue(reg, &regs->dr7);
        break;
      default:
        status = ZX_ERR_INVALID_ARGS;
        break;
    }

    if (status != ZX_OK)
      return status;
  }
  return ZX_OK;
}

// HW Breakpoints --------------------------------------------------------------

zx_status_t SetupHWBreakpoint(uint64_t address, zx_thread_state_debug_regs_t* debug_regs) {
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

zx_status_t RemoveHWBreakpoint(uint64_t address, zx_thread_state_debug_regs_t* debug_regs) {
  // Search for the slot.
  for (int i = 0; i < 4; i++) {
    if (!HWDebugResourceEnabled(debug_regs->dr7, i) || IsWatchpoint(debug_regs->dr7, i)) {
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

// clang-format off

// x86 uses the following bits to represent watchpoint lenghts:
// 00: 1 byte.
// 10: 2 bytes.
// 11: 8 bytes.
// 10: 4 bytes.
//
// The following functions translate between the both.

inline uint64_t x86LenToLength(uint64_t len) {
  switch (len) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 8;
    case 3: return 4;
  }

  FXL_NOTREACHED() << "Invalid len: " << len;
  return 0;
}

inline uint64_t LengthTox86Length(uint64_t len) {
  switch (len) {
    case 1: return 0;
    case 2: return 1;
    case 8: return 2;
    case 4: return 3;
  }

  FXL_NOTREACHED() << "Invalid len: " << len;
  return 0;
}

void SetWatchpointFlags(uint64_t* dr7, int slot, bool active, uint64_t size) {
  switch (slot) {
    case 0: {
      X86_DBG_CONTROL_L0_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW0_SET(dr7, 1);
      X86_DBG_CONTROL_LEN0_SET(dr7, size ? LengthTox86Length(size) : 0);
      return;
    }
    case 1: {
      X86_DBG_CONTROL_L1_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW1_SET(dr7, 1);
      X86_DBG_CONTROL_LEN1_SET(dr7, size ? LengthTox86Length(size) : 0);
      return;
    }
    case 2: {
      X86_DBG_CONTROL_L2_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW2_SET(dr7, 1);
      X86_DBG_CONTROL_LEN2_SET(dr7, size ? LengthTox86Length(size) : 0);
      return;
    }
    case 3: {
      X86_DBG_CONTROL_L3_SET(dr7, active ? 1 : 0);
      X86_DBG_CONTROL_RW3_SET(dr7, 1);
      X86_DBG_CONTROL_LEN3_SET(dr7, size ? LengthTox86Length(size) : 0);
      return;
    }
  }

  FXL_NOTREACHED() << "Invalid slot: " << slot;
}

}  // namespace

uint64_t GetWatchpointLength(uint64_t dr7, int slot) {
  switch (slot) {
    case 0: return x86LenToLength(X86_DBG_CONTROL_LEN0_GET(dr7));
    case 1: return x86LenToLength(X86_DBG_CONTROL_LEN1_GET(dr7));
    case 2: return x86LenToLength(X86_DBG_CONTROL_LEN2_GET(dr7));
    case 3: return x86LenToLength(X86_DBG_CONTROL_LEN3_GET(dr7));
  }

  FXL_NOTREACHED() << "Invalid slot: " << slot;
  return -1;
}

uint64_t WatchpointAddressAlign(uint64_t address, uint64_t size) {
  switch (size) {
    case 1: return address;
    case 2: return address & (uint64_t)(~0b1);
    case 4: return address & (uint64_t)(~0b11);
    case 8: return address & (uint64_t)(~0b111);
  }

  return 0;
}
// clang-format on

std::pair<zx_status_t, int> SetupWatchpoint(zx_thread_state_debug_regs_t* debug_regs,
                                            uint64_t address, uint64_t size) {
  uint64_t aligned_address = WatchpointAddressAlign(address, size);
  if (!aligned_address)
    return {ZX_ERR_INVALID_ARGS, -1};

  // Check alignment.
  if (address != aligned_address)
    return {ZX_ERR_OUT_OF_RANGE, -1};

  // Search for a free slot.
  int slot = -1;
  for (int i = 0; i < 4; i++) {
    if (HWDebugResourceEnabled(debug_regs->dr7, i)) {
      // If it's the same address, we don't need to do anything.
      // For watchpoints, we need to compare against the range.
      if ((debug_regs->dr[i] == address) && GetWatchpointLength(debug_regs->dr7, i) == size) {
        return {ZX_ERR_ALREADY_BOUND, -1};
      }
    } else {
      slot = i;
      break;
    }
  }

  if (slot == -1)
    return {ZX_ERR_NO_RESOURCES, -1};

  // We found a slot, we bind the watchpoint.
  debug_regs->dr[slot] = aligned_address;
  SetWatchpointFlags(&debug_regs->dr7, slot, true, size);

  return {ZX_OK, slot};
}

zx_status_t RemoveWatchpoint(zx_thread_state_debug_regs_t* debug_regs, uint64_t address,
                             uint64_t size) {
  uint64_t aligned_address = WatchpointAddressAlign(address, size);
  if (!aligned_address)
    return ZX_ERR_INVALID_ARGS;

  for (int slot = 0; slot < 4; slot++) {
    if (!IsWatchpoint(debug_regs->dr7, slot))
      continue;

    // Both address and length should match.
    if ((debug_regs->dr[slot] != aligned_address) ||
        GetWatchpointLength(debug_regs->dr7, slot) != size) {
      continue;
    }

    // Clear this breakpoint.
    debug_regs->dr[slot] = 0;
    SetWatchpointFlags(&debug_regs->dr7, slot, false, 0);
    debug_regs->dr7 &= HWDebugResourceD7ClearMask(slot);
    return ZX_OK;
  }

  // We didn't find the address.
  return ZX_ERR_NOT_FOUND;
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

}  // namespace arch
}  // namespace debug_agent
