// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/ipc/decode_exception.h"
#include "src/developer/debug/ipc/register_desc.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/developer/debug/shared/logging/logging.h"

// Notes on x64 architecture:
//
// Intel® 64 and IA-32 Architectures Software Developer’s Manual Volume 3 (3A, 3B, 3C & 3D):
// Chapter 17 holds the debug spefications:
// https://software.intel.com/sites/default/files/managed/a4/60/325383-sdm-vol-2abcd.pdf
//
// Hardware Breakpoints/Watchpoints
// -------------------------------------------------------------------------------------------------
//
// Hardware breakpoints permits to stop a thread when it accesses an address setup in one of the
// hw breakpoints registers. They will work independent whether the address in question is
// read-only or not.
//
// Watchpoints are meant to throw an exception whenever the given address is read or written to,
// depending on the configuration.
//
// DR0 to DR4 registers: There registers are the address to which the hw breakpoint/watchpoint
// refers to. How it is interpreted depends on the associated configuration on the register DR7.
//
// DR6: Debug Status Register.
//
// This register is updated when the CPU encounters a #DB harware exception. This registers permits
// users to interpret the result of an exception, such as if it was a single-step, hardware
// breakpoint, etc.
//
// zircon/system/public/zircon/hw/debug/x86.h holds a good description of what each bit within the
// register means.
//
// DR7: Debug Control Register.
//
// This register is used to establish the breakpoint conditions for the address breakpoint registers
// (DR0-DR3) and to enable debug exceptions for each of them individually.
//
// The following fields are accepted by the user. All other fields are ignored (masked):
//
// - L0, L1, L2, L3: These defines whether breakpoint/watchpoint <n> is enabled or not.
//
// - LEN0, LEN1, LEN2, LEN3: Defines the "length" of the breakpoint/watchpoint.
//                           00: 1 byte.
//                           01: 2 byte. DRn must be 2 byte aligned.
//                           10: 8 byte. DRn must be 8 byte aligned.
//                           11: 4 byte. DRn must be 4 byte aligned.
//                           p
// - RW0, RW1, RW2, RW3: The "mode" of the registers.
//                       00: Only instruction execution (hw breakpoint).
//                       01: Only data write (write watchpoint).
//                       10: Dependant by CR4.DE. Not supported by Zircon.
//                       - CR4.DE = 0: Undefined.
//                       - CR4.DE = 1: Only on I/0 read/write.
//                       11: Only on data read/write (read/write watchpoint).

namespace debug_agent {
namespace arch {

namespace {

using debug_ipc::Register;
using debug_ipc::RegisterID;

// Implements a case statement for calling WriteRegisterValue assuming the Zircon register
// field matches the enum name. This avoids implementation typos where the names don't match.
#define IMPLEMENT_CASE_WRITE_REGISTER_VALUE(name)  \
  case RegisterID::kX64_##name:                    \
    status = WriteRegisterValue(reg, &regs->name); \
    break;

inline debug_ipc::Register CreateRegister(RegisterID id, uint32_t length, const void* val_ptr) {
  debug_ipc::Register reg;
  reg.id = id;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(val_ptr);
  reg.data.assign(ptr, ptr + length);
  return reg;
}

zx_status_t ReadGeneralRegs(const zx::thread& thread, std::vector<debug_ipc::Register>& out) {
  zx_thread_state_general_regs gen_regs;
  zx_status_t status = thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &gen_regs, sizeof(gen_regs));
  if (status != ZX_OK)
    return status;

  SaveGeneralRegs(gen_regs, out);
  return ZX_OK;
}

zx_status_t ReadFPRegs(const zx::thread& thread, std::vector<debug_ipc::Register>& out) {
  zx_thread_state_fp_regs fp_regs;
  zx_status_t status = thread.read_state(ZX_THREAD_STATE_FP_REGS, &fp_regs, sizeof(fp_regs));
  if (status != ZX_OK)
    return status;

  out.push_back(CreateRegister(RegisterID::kX64_fcw, 2u, &fp_regs.fcw));
  out.push_back(CreateRegister(RegisterID::kX64_fsw, 2u, &fp_regs.fsw));
  out.push_back(CreateRegister(RegisterID::kX64_ftw, 2u, &fp_regs.ftw));
  out.push_back(CreateRegister(RegisterID::kX64_fop, 2u, &fp_regs.fop));
  out.push_back(CreateRegister(RegisterID::kX64_fip, 2u, &fp_regs.fip));
  out.push_back(CreateRegister(RegisterID::kX64_fdp, 2u, &fp_regs.fdp));

  // Each entry is 16 bytes long, but only 10 are actually used.
  out.push_back(CreateRegister(RegisterID::kX64_st0, 16u, &fp_regs.st[0]));
  out.push_back(CreateRegister(RegisterID::kX64_st1, 16u, &fp_regs.st[1]));
  out.push_back(CreateRegister(RegisterID::kX64_st2, 16u, &fp_regs.st[2]));
  out.push_back(CreateRegister(RegisterID::kX64_st3, 16u, &fp_regs.st[3]));
  out.push_back(CreateRegister(RegisterID::kX64_st4, 16u, &fp_regs.st[4]));
  out.push_back(CreateRegister(RegisterID::kX64_st5, 16u, &fp_regs.st[5]));
  out.push_back(CreateRegister(RegisterID::kX64_st6, 16u, &fp_regs.st[6]));
  out.push_back(CreateRegister(RegisterID::kX64_st7, 16u, &fp_regs.st[7]));

  return ZX_OK;
}

zx_status_t ReadVectorRegs(const zx::thread& thread, std::vector<debug_ipc::Register>& out) {
  zx_thread_state_vector_regs vec_regs;
  zx_status_t status = thread.read_state(ZX_THREAD_STATE_VECTOR_REGS, &vec_regs, sizeof(vec_regs));
  if (status != ZX_OK)
    return status;

  out.push_back(CreateRegister(RegisterID::kX64_mxcsr, 4u, &vec_regs.mxcsr));

  auto base = static_cast<uint32_t>(RegisterID::kX64_zmm0);
  for (size_t i = 0; i < 32; i++) {
    auto reg_id = static_cast<RegisterID>(base + i);
    out.push_back(CreateRegister(reg_id, 64u, &vec_regs.zmm[i]));
  }

  return ZX_OK;
}

zx_status_t ReadDebugRegs(const zx::thread& thread, std::vector<debug_ipc::Register>& out) {
  zx_thread_state_debug_regs_t debug_regs;
  zx_status_t status =
      thread.read_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs));
  if (status != ZX_OK)
    return status;

  out.push_back(CreateRegister(RegisterID::kX64_dr0, 8u, &debug_regs.dr[0]));
  out.push_back(CreateRegister(RegisterID::kX64_dr1, 8u, &debug_regs.dr[1]));
  out.push_back(CreateRegister(RegisterID::kX64_dr2, 8u, &debug_regs.dr[2]));
  out.push_back(CreateRegister(RegisterID::kX64_dr3, 8u, &debug_regs.dr[3]));
  out.push_back(CreateRegister(RegisterID::kX64_dr6, 8u, &debug_regs.dr6));
  out.push_back(CreateRegister(RegisterID::kX64_dr7, 8u, &debug_regs.dr7));

  return ZX_OK;
}

// Adapter class to allow the exception decoder to get the debug registers if needed.
class ExceptionInfo : public debug_ipc::X64ExceptionInfo {
 public:
  explicit ExceptionInfo(const zx::thread& thread) : thread_(thread) {}

  std::optional<debug_ipc::X64ExceptionInfo::DebugRegs> FetchDebugRegs() const override {
    zx_thread_state_debug_regs_t debug_regs;
    zx_status_t status =
        thread_.read_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs));
    if (status != ZX_OK) {
      DEBUG_LOG(Archx64) << "Could not get debug regs: " << zx_status_get_string(status);
      return std::nullopt;
    }

    debug_ipc::X64ExceptionInfo::DebugRegs ret;

    ret.dr0 = debug_regs.dr[0];
    ret.dr1 = debug_regs.dr[1];
    ret.dr2 = debug_regs.dr[2];
    ret.dr3 = debug_regs.dr[3];
    ret.dr6 = debug_regs.dr6;
    ret.dr7 = debug_regs.dr7;

    return ret;
  }

 private:
  const zx::thread& thread_;
};

}  // namespace

const BreakInstructionType kBreakInstruction = 0xCC;

// An X86 exception is 1 byte and a breakpoint exception is triggered with RIP pointing to the
// following instruction.
const int64_t kExceptionOffsetForSoftwareBreakpoint = 1;

::debug_ipc::Arch GetCurrentArch() { return ::debug_ipc::Arch::kX64; }

void SaveGeneralRegs(const zx_thread_state_general_regs& input,
                     std::vector<debug_ipc::Register>& out) {
  out.push_back(CreateRegister(RegisterID::kX64_rax, 8u, &input.rax));
  out.push_back(CreateRegister(RegisterID::kX64_rbx, 8u, &input.rbx));
  out.push_back(CreateRegister(RegisterID::kX64_rcx, 8u, &input.rcx));
  out.push_back(CreateRegister(RegisterID::kX64_rdx, 8u, &input.rdx));
  out.push_back(CreateRegister(RegisterID::kX64_rsi, 8u, &input.rsi));
  out.push_back(CreateRegister(RegisterID::kX64_rdi, 8u, &input.rdi));
  out.push_back(CreateRegister(RegisterID::kX64_rbp, 8u, &input.rbp));
  out.push_back(CreateRegister(RegisterID::kX64_rsp, 8u, &input.rsp));
  out.push_back(CreateRegister(RegisterID::kX64_r8, 8u, &input.r8));
  out.push_back(CreateRegister(RegisterID::kX64_r9, 8u, &input.r9));
  out.push_back(CreateRegister(RegisterID::kX64_r10, 8u, &input.r10));
  out.push_back(CreateRegister(RegisterID::kX64_r11, 8u, &input.r11));
  out.push_back(CreateRegister(RegisterID::kX64_r12, 8u, &input.r12));
  out.push_back(CreateRegister(RegisterID::kX64_r13, 8u, &input.r13));
  out.push_back(CreateRegister(RegisterID::kX64_r14, 8u, &input.r14));
  out.push_back(CreateRegister(RegisterID::kX64_r15, 8u, &input.r15));
  out.push_back(CreateRegister(RegisterID::kX64_rip, 8u, &input.rip));
  out.push_back(CreateRegister(RegisterID::kX64_rflags, 8u, &input.rflags));
  out.push_back(CreateRegister(RegisterID::kX64_fsbase, 8u, &input.fs_base));
  out.push_back(CreateRegister(RegisterID::kX64_gsbase, 8u, &input.gs_base));
}

zx_status_t ReadRegisters(const zx::thread& thread, const debug_ipc::RegisterCategory& cat,
                          std::vector<debug_ipc::Register>& out) {
  switch (cat) {
    case debug_ipc::RegisterCategory::kGeneral:
      return ReadGeneralRegs(thread, out);
    case debug_ipc::RegisterCategory::kFloatingPoint:
      return ReadFPRegs(thread, out);
    case debug_ipc::RegisterCategory::kVector:
      return ReadVectorRegs(thread, out);
    case debug_ipc::RegisterCategory::kDebug:
      return ReadDebugRegs(thread, out);
    case debug_ipc::RegisterCategory::kNone:
    case debug_ipc::RegisterCategory::kLast:
      FX_LOGS(ERROR) << "Asking to get none/last category";
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t WriteRegisters(zx::thread& thread, const debug_ipc::RegisterCategory& category,
                           const std::vector<debug_ipc::Register>& registers) {
  switch (category) {
    case debug_ipc::RegisterCategory::kGeneral: {
      zx_thread_state_general_regs_t regs;
      zx_status_t res = thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
      if (res != ZX_OK)
        return res;

      // Overwrite the values.
      res = WriteGeneralRegisters(registers, &regs);
      if (res != ZX_OK)
        return res;

      return thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
    }
    case debug_ipc::RegisterCategory::kFloatingPoint: {
      zx_thread_state_fp_regs_t regs;
      zx_status_t res = thread.read_state(ZX_THREAD_STATE_FP_REGS, &regs, sizeof(regs));
      if (res != ZX_OK)
        return res;

      // Overwrite the values.
      res = WriteFloatingPointRegisters(registers, &regs);
      if (res != ZX_OK)
        return res;

      return thread.write_state(ZX_THREAD_STATE_FP_REGS, &regs, sizeof(regs));
    }
    case debug_ipc::RegisterCategory::kVector: {
      zx_thread_state_vector_regs_t regs;
      zx_status_t res = thread.read_state(ZX_THREAD_STATE_VECTOR_REGS, &regs, sizeof(regs));
      if (res != ZX_OK)
        return res;

      // Overwrite the values.
      res = WriteVectorRegisters(registers, &regs);
      if (res != ZX_OK)
        return res;

      return thread.write_state(ZX_THREAD_STATE_VECTOR_REGS, &regs, sizeof(regs));
    }
    case debug_ipc::RegisterCategory::kDebug: {
      zx_thread_state_debug_regs_t regs;
      zx_status_t res = thread.read_state(ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(regs));
      if (res != ZX_OK)
        return res;

      res = WriteDebugRegisters(registers, &regs);
      if (res != ZX_OK)
        return res;

      return thread.write_state(ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(regs));
    }
    case debug_ipc::RegisterCategory::kNone:
    case debug_ipc::RegisterCategory::kLast:
      break;
  }
  FX_NOTREACHED();
  return ZX_ERR_INVALID_ARGS;
}

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

debug_ipc::ExceptionType DecodeExceptionType(const zx::thread& thread, uint32_t exception_type) {
  ExceptionInfo info(thread);
  return debug_ipc::DecodeException(exception_type, info);
}

debug_ipc::ExceptionRecord FillExceptionRecord(const zx_exception_report_t& in) {
  debug_ipc::ExceptionRecord record;

  record.valid = true;
  record.arch.x64.vector = in.context.arch.u.x86_64.vector;
  record.arch.x64.err_code = in.context.arch.u.x86_64.err_code;
  record.arch.x64.cr2 = in.context.arch.u.x86_64.cr2;

  return record;
}

uint64_t NextInstructionForSoftwareExceptionAddress(uint64_t exception_addr) {
  // Exception address is the one following the instruction that caused it, so nothing needs to be
  // done.
  return exception_addr;
}

bool IsBreakpointInstruction(BreakInstructionType instruction) {
  // This handles the normal encoding of debug breakpoints (0xCC). It's also possible to cause an
  // interrupt 3 to happen using the opcode sequence 0xCD 0x03 but this has slightly different
  // semantics and no assemblers emit this. We can't easily check for that here since the
  // computation for the instruction address that is passed in assumes a 1-byte instruction. It
  // should be OK to ignore this case in practice.
  return instruction == kBreakInstruction;
}

uint64_t BreakpointInstructionForHardwareExceptionAddress(uint64_t exception_addr) {
  // x86 returns the instruction *about* to be executed when hitting the hw breakpoint.
  return exception_addr;
}

}  // namespace arch
}  // namespace debug_agent
