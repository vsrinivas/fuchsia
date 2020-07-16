// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch_x64_helpers.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/hw/debug/x86.h>

#include <vector>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/arch_helpers.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/lib/fxl/strings/string_printf.h"

using namespace debug_ipc;

namespace debug_agent {
namespace arch {

// Implements a case statement for calling WriteRegisterValue assuming the Zircon register
// field matches the enum name. This avoids implementation typos where the names don't match.
#define IMPLEMENT_CASE_WRITE_REGISTER_VALUE(name)  \
  case RegisterID::kX64_##name:                    \
    status = WriteRegisterValue(reg, &regs->name); \
    break;

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

}  // namespace arch
}  // namespace debug_agent
