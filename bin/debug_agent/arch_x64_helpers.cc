// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/arch_x64_helpers.h"

#include "garnet/bin/debug_agent/arch.h"
#include "lib/fxl/logging.h"

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

zx_status_t SetupDebugBreakpoint(uint64_t address,
                                 zx_thread_state_debug_regs_t* debug_regs) {
  // Search for an unset register.
  // TODO(donosoc): This doesn't check that the address is already set.
  const DebugRegMask* slot = nullptr;
  for (size_t i = 0; i < 4; i++) {
    const DebugRegMask* mask = GetDebugRegisterMasks(i);
    if (!FLAG_VALUE(debug_regs->dr7, mask->bp_mask)) {
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

zx_status_t RemoveDebugBreakpoint(uint64_t address,
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

void PrintDebugRegisters(const zx_thread_state_debug_regs_t& regs) {
  FXL_LOG(INFO) << "Regs: " << std::endl
                << "DR0: 0x" << std::hex << regs.dr[0] << std::endl
                << "DR1: 0x" << std::hex << regs.dr[1] << std::endl
                << "DR2: 0x" << std::hex << regs.dr[2] << std::endl
                << "DR3: 0x" << std::hex << regs.dr[3] << std::endl
                << "DR6: 0x" << std::hex << regs.dr6 << std::endl
                << "DR7: 0x" << std::hex << regs.dr7 << std::endl;
}

}  // namespace arch
}  // namespace debug_agent
