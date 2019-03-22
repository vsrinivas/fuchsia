// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_arm64_helpers.h"

#include "src/developer/debug/debug_agent/arch.h"

namespace debug_agent {
namespace arch {

zx_status_t SetupHWBreakpoint(uint64_t address,
                              zx_thread_state_debug_regs_t* debug_regs) {
  // We search for an unset register.
  int slot = -1;
  for (int i = 0; i < debug_regs->hw_bps_count; i++) {
    auto& hw_bp = debug_regs->hw_bps[i];

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
    return ZX_ERR_NO_RESOURCES;

  debug_regs->hw_bps[slot].dbgbcr |= 1u;
  debug_regs->hw_bps[slot].dbgbvr = address;
  return ZX_OK;
}

zx_status_t RemoveHWBreakpoint(uint64_t address,
                               zx_thread_state_debug_regs_t* debug_regs) {
  // Search for an breakpoint with this address.
  int slot = -1;
  for (int i = 0; i < debug_regs->hw_bps_count; i++) {
    auto& hw_bp = debug_regs->hw_bps[i];
    if (hw_bp.dbgbvr == address) {
      slot = i;
      break;
    }
  }

  if (slot == -1)
    return ZX_ERR_OUT_OF_RANGE;

  debug_regs->hw_bps[slot].dbgbcr &= ~1u;
  debug_regs->hw_bps[slot].dbgbvr = 0;
  return ZX_OK;
}

}  // namespace arch
}  // namespace debug_agent
