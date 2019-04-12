// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_x64_helpers.h"

#include <vector>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {
namespace arch {

namespace {

struct DebugRegMask {
  int index = -1;
  uint64_t bp_mask = 0;   // Enable mask within DR7
  uint64_t rw_mask = 0;   // RW mask within DR7
  uint64_t len_mask = 0;  // LEN mask within DR7
};

const DebugRegMask* GetDebugRegisterMasks(size_t index) {
  static std::vector<DebugRegMask> masks = {
      {0, kDR7L0, kDR7RW0 | (kDR7RW0 << 1), kDR7LEN0 | (kDR7LEN0 << 1)},
      {1, kDR7L1, kDR7RW1 | (kDR7RW1 << 1), kDR7LEN1 | (kDR7LEN1 << 1)},
      {2, kDR7L2, kDR7RW2 | (kDR7RW2 << 1), kDR7LEN2 | (kDR7LEN2 << 1)},
      {3, kDR7L3, kDR7RW3 | (kDR7RW3 << 1), kDR7LEN3 | (kDR7LEN3 << 1)},
  };
  FXL_DCHECK(index < masks.size());
  return &masks[index];
}

}  // namespace

zx_status_t SetupHWBreakpoint(uint64_t address,
                              zx_thread_state_debug_regs_t* debug_regs) {
  // Search for an unset register.
  // TODO(donosoc): This doesn't check that the address is already set.
  const DebugRegMask* slot = nullptr;
  for (size_t i = 0; i < 4; i++) {
    const DebugRegMask* mask = GetDebugRegisterMasks(i);
    // If it's the same address or it's free, we found our slot.
    bool active = FLAG_VALUE(debug_regs->dr7, mask->bp_mask);
    if (debug_regs->dr[i] == address || !active) {
      slot = mask;
      break;
    }
  }

  if (!slot)
    return ZX_ERR_NO_RESOURCES;

  debug_regs->dr[slot->index] = address;
  // Modify the DR7 register.
  // TODO(donosoc): For now only add execution breakpoints.
  uint64_t dr7 = debug_regs->dr7;
  dr7 |= slot->bp_mask;  // Activate the breakpoint.
  uint64_t mask = ~(slot->rw_mask);
  // TODO(donosoc): Handle LEN properties of the breakpoint.
  dr7 &= mask;
  debug_regs->dr7 = dr7;

  return ZX_OK;
}

zx_status_t RemoveHWBreakpoint(uint64_t address,
                               zx_thread_state_debug_regs_t* debug_regs) {
  // Search for the address.
  bool found = false;
  for (size_t i = 0; i < 4; i++) {
    if (address != debug_regs->dr[i])
      continue;

    const DebugRegMask* mask = GetDebugRegisterMasks(i);
    // Only unset the
    uint64_t dr7 = debug_regs->dr7;
    dr7 &= ~(mask->bp_mask);  // Disable the breakpoint.

    debug_regs->dr[i] = 0;
    debug_regs->dr7 = dr7;

    found = true;
  }

  // No register found, we warn the caller. No change was issued.
  if (!found)
    return ZX_ERR_OUT_OF_RANGE;

  return ZX_OK;
}

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
     << "DR7: 0x" << std::hex << regs.dr7 << std::endl;

  return ss.str();
}

std::string DR6ToString(uint64_t dr6) {
  return fxl::StringPrintf(
      "0x%lx: B0=%d, B1=%d, B2=%d, B3=%d, BD=%d, BS=%d, BT=%d", dr6,
      X86_FLAG_VALUE(dr6, Dr6B0), X86_FLAG_VALUE(dr6, Dr6B1),
      X86_FLAG_VALUE(dr6, Dr6B2), X86_FLAG_VALUE(dr6, Dr6B3),
      X86_FLAG_VALUE(dr6, Dr6BD), X86_FLAG_VALUE(dr6, Dr6BS),
      X86_FLAG_VALUE(dr6, Dr6BT));
}

}  // namespace arch
}  // namespace debug_agent
