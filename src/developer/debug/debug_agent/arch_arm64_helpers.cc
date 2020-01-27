// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_arm64_helpers.h"

#include <zircon/hw/debug/arm64.h>
#include <zircon/syscalls/debug.h>

#include <sstream>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/arch_helpers.h"
#include "src/developer/debug/shared/arch_arm64.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {
namespace arch {

using debug_ipc::Register;
using debug_ipc::RegisterID;

// Implements a case statement for calling WriteRegisterValue assuming the Zircon register
// field matches the enum name. This avoids implementation typos where the names don't match.
#define IMPLEMENT_CASE_WRITE_REGISTER_VALUE(name)  \
  case RegisterID::kARMv8_##name:                  \
    status = WriteRegisterValue(reg, &regs->name); \
    break;

zx_status_t SetupHWBreakpoint(uint64_t address, zx_thread_state_debug_regs_t* debug_regs) {
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

zx_status_t RemoveHWBreakpoint(uint64_t address, zx_thread_state_debug_regs_t* debug_regs) {
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

// Watchpoint Installation -------------------------------------------------------------------------

namespace {

uint32_t ValidateRange(const debug_ipc::AddressRange& range) {
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

  FXL_NOTREACHED() << "Invalid breakpoint type: " << static_cast<uint32_t>(type);
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
    FXL_NOTREACHED() << "Invalid range size: " << range.size();
  }

  // Set type.
  ARM64_DBGWCR_LSC_SET(dbgwcr, GetWatchpointWriteFlag(type));

  // Set enabled.
  ARM64_DBGWCR_E_SET(dbgwcr, 1);
}

}  // namespace

WatchpointInstallationResult SetupWatchpoint(zx_thread_state_debug_regs_t* regs,
                                             debug_ipc::BreakpointType type,
                                             const debug_ipc::AddressRange& range,
                                             uint32_t watchpoint_count) {
  FXL_DCHECK(watchpoint_count <= 16);
  if (!IsWatchpointType(type)) {
    FXL_NOTREACHED() << "Requires a watchpoint type, received "
                     << debug_ipc::BreakpointTypeToString(type);
    return CreateResult(ZX_ERR_INVALID_ARGS);
  }

  uint32_t base_address = ValidateRange(range);
  if (base_address == 0)
    return CreateResult(ZX_ERR_OUT_OF_RANGE);

  // Search for a free slot.
  int slot = -1;
  for (uint32_t i = 0; i < watchpoint_count; i++) {
    if (regs->hw_wps[i].dbgwvr == 0) {
      slot = i;
      break;
    }

    // If it's the same address, we need top compare length.
    uint32_t length = GetWatchpointLength(regs->hw_wps[i].dbgwcr);
    if (regs->hw_wps[i].dbgwvr == base_address && length == range.size())
      return CreateResult(ZX_ERR_ALREADY_BOUND);
  }

  if (slot == -1)
    return CreateResult(ZX_ERR_NO_RESOURCES);

  // We found a slot, we bind the watchpoint.
  regs->hw_wps[slot].dbgwvr = base_address;
  SetWatchpointFlags(&regs->hw_wps[slot].dbgwcr, type, base_address, range);

  return CreateResult(ZX_OK, range, slot);
}

zx_status_t RemoveWatchpoint(zx_thread_state_debug_regs_t* regs,
                             const debug_ipc::AddressRange& range, uint32_t watchpoint_count) {
  FXL_DCHECK(watchpoint_count <= 16);

  uint32_t base_address = ValidateRange(range);
  if (base_address == 0)
    return ZX_ERR_OUT_OF_RANGE;

  // Search for a slot that matches.
  int slot = -1;
  for (uint32_t i = 0; i < watchpoint_count; i++) {
    if (regs->hw_wps[i].dbgwvr == 0)
      continue;

    // If it's the same address, we need to compare length.
    uint32_t length = GetWatchpointLength(regs->hw_wps[i].dbgwcr);
    if (regs->hw_wps[i].dbgwvr == base_address && length == range.size()) {
      slot = i;
      break;
    }
  }

  if (slot == -1)
    return ZX_ERR_NOT_FOUND;

  // Clear the slot.
  regs->hw_wps[slot].dbgwcr = 0;
  regs->hw_wps[slot].dbgwvr = 0;

  return ZX_OK;
}

std::string DebugRegistersToString(const zx_thread_state_debug_regs_t& regs) {
  std::stringstream ss;

  ss << "ESR: 0x" << std::hex << regs.esr << std::endl;

  ss << "HW breakpoints: " << std::endl;
  for (size_t i = 0; i < std::size(regs.hw_bps); i++) {
    uint32_t dbgbcr = regs.hw_bps[i].dbgbcr;
    uint64_t dbgbvr = regs.hw_bps[i].dbgbvr;
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
  for (size_t i = 0; i < std::size(regs.hw_wps); i++) {
    uint32_t dbgwcr = regs.hw_wps[i].dbgwcr;
    uint64_t dbgwvr = regs.hw_wps[i].dbgwvr;
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

zx_status_t WriteGeneralRegisters(const std::vector<Register>& updates,
                                  zx_thread_state_general_regs_t* regs) {
  uint32_t begin_general = static_cast<uint32_t>(RegisterID::kARMv8_x0);
  uint32_t last_general = static_cast<uint32_t>(RegisterID::kARMv8_x29);

  for (const Register& reg : updates) {
    zx_status_t status = ZX_OK;
    if (reg.data.size() != 8)
      return ZX_ERR_INVALID_ARGS;

    uint32_t id = static_cast<uint32_t>(reg.id);
    if (id >= begin_general && id <= last_general) {
      // General register array.
      status = WriteRegisterValue(reg, &regs->r[id - begin_general]);
    } else {
      switch (reg.id) {
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(lr);
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(sp);
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(pc);
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(cpsr);
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
  uint32_t begin_vector = static_cast<uint32_t>(RegisterID::kARMv8_v0);
  uint32_t last_vector = static_cast<uint32_t>(RegisterID::kARMv8_v31);

  for (const auto& reg : updates) {
    zx_status_t status = ZX_OK;
    uint32_t id = static_cast<uint32_t>(reg.id);

    if (id >= begin_vector && id <= last_vector) {
      status = WriteRegisterValue(reg, &regs->v[id - begin_vector]);
    } else {
      switch (reg.id) {
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(fpcr);
        IMPLEMENT_CASE_WRITE_REGISTER_VALUE(fpsr);
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

zx_status_t WriteDebugRegisters(const std::vector<Register>& updates,
                                zx_thread_state_debug_regs_t* regs) {
  uint32_t begin_bcr = static_cast<uint32_t>(RegisterID::kARMv8_dbgbcr0_el1);
  uint32_t last_bcr = static_cast<uint32_t>(RegisterID::kARMv8_dbgbcr15_el1);

  uint32_t begin_bvr = static_cast<uint32_t>(RegisterID::kARMv8_dbgbvr0_el1);
  uint32_t last_bvr = static_cast<uint32_t>(RegisterID::kARMv8_dbgbvr15_el1);

  // TODO(bug 40992) Add ARM64 hardware watchpoint registers here.

  for (const auto& reg : updates) {
    zx_status_t status = ZX_OK;
    uint32_t id = static_cast<uint32_t>(reg.id);

    if (id >= begin_bcr && id <= last_bcr) {
      status = WriteRegisterValue(reg, &regs->hw_bps[id - begin_bcr].dbgbcr);
    } else if (id >= begin_bvr && id <= last_bvr) {
      status = WriteRegisterValue(reg, &regs->hw_bps[id - begin_bvr].dbgbvr);
    } else {
      status = ZX_ERR_INVALID_ARGS;
    }

    if (status != ZX_OK)
      return status;
  }
  return ZX_OK;
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
      FXL_NOTREACHED() << "Wrong bas value: 0x" << std::hex << bas;
      return 0;
  }
  // clang-format on
}

}  // namespace arch
}  // namespace debug_agent
