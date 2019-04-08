// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_arm64_helpers.h"

#include <sstream>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/shared/arch_arm64.h"
#include "src/lib/fxl/strings/string_printf.h"

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

  debug_regs->hw_bps[slot].dbgbcr = 0;
  debug_regs->hw_bps[slot].dbgbvr = 0;
  return ZX_OK;
}

zx_status_t ReadDebugRegs(const zx::thread& thread,
                          zx_thread_state_debug_regs_t* debug_regs) {
  return thread.read_state(ZX_THREAD_STATE_DEBUG_REGS, debug_regs,
                           sizeof(zx_thread_state_debug_regs_t));
}

zx_status_t WriteDebugRegs(const zx::thread& thread,
                           const zx_thread_state_debug_regs_t& debug_regs) {
  return thread.write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                            sizeof(zx_thread_state_debug_regs_t));
}

debug_ipc::NotifyException::Type DecodeESR(uint32_t esr) {
  uint32_t ec = Arm64ExtractECFromESR(esr);
  switch (ec) {
    case 0b111000: /* BRK from arm32 */
    case 0b111100: /* BRK from arm64 */
      return debug_ipc::NotifyException::Type::kSoftware;
    case 0b110000: /* HW breakpoit from a lower level */
    case 0b110001: /* HW breakpoint from same level */
      return debug_ipc::NotifyException::Type::kHardware;
    case 0b110010: /* software step from lower level */
    case 0b110011: /* software step from same level */
      return debug_ipc::NotifyException::Type::kSingleStep;
    default:
      break;
  }

  return debug_ipc::NotifyException::Type::kGeneral;
}

std::string DebugRegistersToString(const zx_thread_state_debug_regs_t& regs) {
  std::stringstream ss;
  for (size_t i = 0; i < debug_ipc::kMaxArm64HWBreakpoints; i++) {
    uint32_t dbgbcr = regs.hw_bps[i].dbgbcr;
    uint64_t dbgbvr = regs.hw_bps[i].dbgbvr;

    ss << fxl::StringPrintf(
        "%lu. DBGBVR: 0x%lx, DBGBCR: E=%d, PMC=%d, BAS=%d, HMC=%d, SSC=%d, "
        "LBN=%d, BT=%d",
        i, dbgbvr, ARM64_FLAG_VALUE(dbgbcr, DBGBCR, E),
        ARM64_FLAG_VALUE(dbgbcr, DBGBCR, PMC),
        ARM64_FLAG_VALUE(dbgbcr, DBGBCR, BAS),
        ARM64_FLAG_VALUE(dbgbcr, DBGBCR, HMC),
        ARM64_FLAG_VALUE(dbgbcr, DBGBCR, SSC),
        ARM64_FLAG_VALUE(dbgbcr, DBGBCR, LBN),
        ARM64_FLAG_VALUE(dbgbcr, DBGBCR, BT));
    ss << std::endl;
  }
  ss << "ESR: 0x" << std::hex << regs.esr;
  return ss.str();
}

}  // namespace arch
}  // namespace debug_agent
