// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

}  // namespace arch
}  // namespace debug_agent
