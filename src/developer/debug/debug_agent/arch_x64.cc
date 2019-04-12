// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch.h"

#include <zircon/syscalls/exception.h>

#include "src/lib/fxl/logging.h"
#include "src/developer/debug/debug_agent/arch_x64_helpers.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/ipc/register_desc.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {
namespace arch {

const BreakInstructionType kBreakInstruction = 0xCC;

uint64_t ArchProvider::BreakpointInstructionForSoftwareExceptionAddress(
    uint64_t exception_addr) {
  // An X86 exception is 1 byte and a breakpoint exception is triggered with
  // RIP pointing to the following instruction.
  return exception_addr - 1;
}

uint64_t ArchProvider::NextInstructionForSoftwareExceptionAddress(
    uint64_t exception_addr) {
  // Exception address is the one following the instruction that caused it,
  // so nothing needs to be done.
  return exception_addr;
}

bool ArchProvider::IsBreakpointInstruction(zx::process& process,
                                           uint64_t address) {
  uint8_t data;
  size_t actual_read = 0;
  if (process.read_memory(address, &data, 1, &actual_read) != ZX_OK ||
      actual_read != 1)
    return false;

  // This handles the normal encoding of debug breakpoints (0xCC). It's also
  // possible to cause an interrupt 3 to happen using the opcode sequence
  // 0xCD 0x03 but this has slightly different semantics and no assemblers emit
  // this. We can't easily check for that here since the computation for the
  // instruction address that is passed in assumes a 1-byte instruction. It
  // should be OK to ignore this case in practice.
  return data == kBreakInstruction;
}

uint64_t* ArchProvider::IPInRegs(zx_thread_state_general_regs* regs) {
  return &regs->rip;
}
uint64_t* ArchProvider::SPInRegs(zx_thread_state_general_regs* regs) {
  return &regs->rsp;
}
uint64_t* ArchProvider::BPInRegs(zx_thread_state_general_regs* regs) {
  return &regs->rbp;
}

::debug_ipc::Arch ArchProvider::GetArch() { return ::debug_ipc::Arch::kX64; }

namespace {

using debug_ipc::RegisterID;

inline debug_ipc::Register CreateRegister(RegisterID id, uint32_t length,
                                          const void* val_ptr) {
  debug_ipc::Register reg;
  reg.id = id;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(val_ptr);
  reg.data.assign(ptr, ptr + length);
  return reg;
}

inline zx_status_t ReadGeneralRegs(const zx::thread& thread,
                                   std::vector<debug_ipc::Register>* out) {
  // We get the general state registers.
  zx_thread_state_general_regs gen_regs;
  zx_status_t status = thread.read_state(ZX_THREAD_STATE_GENERAL_REGS,
                                         &gen_regs, sizeof(gen_regs));
  if (status != ZX_OK)
    return status;

  out->push_back(CreateRegister(RegisterID::kX64_rax, 8u, &gen_regs.rax));
  out->push_back(CreateRegister(RegisterID::kX64_rbx, 8u, &gen_regs.rbx));
  out->push_back(CreateRegister(RegisterID::kX64_rcx, 8u, &gen_regs.rcx));
  out->push_back(CreateRegister(RegisterID::kX64_rdx, 8u, &gen_regs.rdx));
  out->push_back(CreateRegister(RegisterID::kX64_rsi, 8u, &gen_regs.rsi));
  out->push_back(CreateRegister(RegisterID::kX64_rdi, 8u, &gen_regs.rdi));
  out->push_back(CreateRegister(RegisterID::kX64_rbp, 8u, &gen_regs.rbp));
  out->push_back(CreateRegister(RegisterID::kX64_rsp, 8u, &gen_regs.rsp));
  out->push_back(CreateRegister(RegisterID::kX64_r8, 8u, &gen_regs.r8));
  out->push_back(CreateRegister(RegisterID::kX64_r9, 8u, &gen_regs.r9));
  out->push_back(CreateRegister(RegisterID::kX64_r10, 8u, &gen_regs.r10));
  out->push_back(CreateRegister(RegisterID::kX64_r11, 8u, &gen_regs.r11));
  out->push_back(CreateRegister(RegisterID::kX64_r12, 8u, &gen_regs.r12));
  out->push_back(CreateRegister(RegisterID::kX64_r13, 8u, &gen_regs.r13));
  out->push_back(CreateRegister(RegisterID::kX64_r14, 8u, &gen_regs.r14));
  out->push_back(CreateRegister(RegisterID::kX64_r15, 8u, &gen_regs.r15));
  out->push_back(CreateRegister(RegisterID::kX64_rip, 8u, &gen_regs.rip));
  out->push_back(CreateRegister(RegisterID::kX64_rflags, 8u, &gen_regs.rflags));

  return ZX_OK;
}

inline zx_status_t ReadFPRegs(const zx::thread& thread,
                              std::vector<debug_ipc::Register>* out) {
  zx_thread_state_fp_regs fp_regs;
  zx_status_t status =
      thread.read_state(ZX_THREAD_STATE_FP_REGS, &fp_regs, sizeof(fp_regs));
  if (status != ZX_OK)
    return status;

  out->push_back(CreateRegister(RegisterID::kX64_fcw, 2u, &fp_regs.fcw));
  out->push_back(CreateRegister(RegisterID::kX64_fsw, 2u, &fp_regs.fsw));
  out->push_back(CreateRegister(RegisterID::kX64_ftw, 2u, &fp_regs.ftw));
  out->push_back(CreateRegister(RegisterID::kX64_fop, 2u, &fp_regs.fop));
  out->push_back(CreateRegister(RegisterID::kX64_fip, 2u, &fp_regs.fip));
  out->push_back(CreateRegister(RegisterID::kX64_fdp, 2u, &fp_regs.fdp));

  // Each entry is 16 bytes long, but only 10 are actually used.
  out->push_back(CreateRegister(RegisterID::kX64_st0, 16u, &fp_regs.st[0]));
  out->push_back(CreateRegister(RegisterID::kX64_st1, 16u, &fp_regs.st[1]));
  out->push_back(CreateRegister(RegisterID::kX64_st2, 16u, &fp_regs.st[2]));
  out->push_back(CreateRegister(RegisterID::kX64_st3, 16u, &fp_regs.st[3]));
  out->push_back(CreateRegister(RegisterID::kX64_st4, 16u, &fp_regs.st[4]));
  out->push_back(CreateRegister(RegisterID::kX64_st5, 16u, &fp_regs.st[5]));
  out->push_back(CreateRegister(RegisterID::kX64_st6, 16u, &fp_regs.st[6]));
  out->push_back(CreateRegister(RegisterID::kX64_st7, 16u, &fp_regs.st[7]));

  return ZX_OK;
}

inline zx_status_t ReadVectorRegs(const zx::thread& thread,
                                  std::vector<debug_ipc::Register>* out) {
  zx_thread_state_vector_regs vec_regs;
  zx_status_t status = thread.read_state(ZX_THREAD_STATE_VECTOR_REGS, &vec_regs,
                                         sizeof(vec_regs));
  if (status != ZX_OK)
    return status;

  out->push_back(CreateRegister(RegisterID::kX64_mxcsr, 4u, &vec_regs.mxcsr));

  // TODO(donosoc): For now there is no support of AVX-512 within zircon,
  //                so we're not sending over that data, only AVX.
  //                Enable it when AVX-512 is done.
  auto base = static_cast<uint32_t>(RegisterID::kX64_ymm0);
  for (size_t i = 0; i < 16; i++) {
    auto reg_id = static_cast<RegisterID>(base + i);
    out->push_back(CreateRegister(reg_id, 32u, &vec_regs.zmm[i]));
  }

  return ZX_OK;
}

// TODO: Enable this when the zircon patch for debug registers lands.

inline zx_status_t ReadDebugRegs(const zx::thread& thread,
                                 std::vector<debug_ipc::Register>* out) {
  zx_thread_state_debug_regs_t debug_regs;
  zx_status_t status = thread.read_state(ZX_THREAD_STATE_DEBUG_REGS,
                                         &debug_regs, sizeof(debug_regs));
  if (status != ZX_OK)
    return status;

  out->push_back(CreateRegister(RegisterID::kX64_dr0, 8u, &debug_regs.dr[0]));
  out->push_back(CreateRegister(RegisterID::kX64_dr1, 8u, &debug_regs.dr[1]));
  out->push_back(CreateRegister(RegisterID::kX64_dr2, 8u, &debug_regs.dr[2]));
  out->push_back(CreateRegister(RegisterID::kX64_dr3, 8u, &debug_regs.dr[3]));
  out->push_back(CreateRegister(RegisterID::kX64_dr6, 8u, &debug_regs.dr6));
  out->push_back(CreateRegister(RegisterID::kX64_dr7, 8u, &debug_regs.dr7));

  return ZX_OK;
}

}  // namespace

zx_status_t ArchProvider::ReadRegisters(
    const debug_ipc::RegisterCategory::Type& cat, const zx::thread& thread,
    std::vector<debug_ipc::Register>* out) {
  switch (cat) {
    case debug_ipc::RegisterCategory::Type::kGeneral:
      return ReadGeneralRegs(thread, out);
    case debug_ipc::RegisterCategory::Type::kFP:
      return ReadFPRegs(thread, out);
    case debug_ipc::RegisterCategory::Type::kVector:
      return ReadVectorRegs(thread, out);
    case debug_ipc::RegisterCategory::Type::kDebug:
      return ReadDebugRegs(thread, out);
    case debug_ipc::RegisterCategory::Type::kNone:
      FXL_LOG(ERROR) << "Asking to get none category";
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t ArchProvider::WriteRegisters(const debug_ipc::RegisterCategory& cat,
                                         zx::thread* thread) {
  switch (cat.type) {
    case debug_ipc::RegisterCategory::Type::kGeneral: {
      zx_thread_state_general_regs_t regs;
      zx_status_t res =
          thread->read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
      if (res != ZX_OK)
        return res;

      // Ovewrite the values.
      res = WriteGeneralRegisters(cat.registers, &regs);
      if (res != ZX_OK)
        return res;

      return thread->write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs,
                                 sizeof(regs));
    }
    case debug_ipc::RegisterCategory::Type::kFP:
    case debug_ipc::RegisterCategory::Type::kVector:
    case debug_ipc::RegisterCategory::Type::kDebug:
      return ZX_ERR_NOT_SUPPORTED;
    case debug_ipc::RegisterCategory::Type::kNone:
      break;
  }
  FXL_NOTREACHED();
  return ZX_ERR_INVALID_ARGS;
}

// Hardware Exceptions ---------------------------------------------------------

uint64_t ArchProvider::BreakpointInstructionForHardwareExceptionAddress(
    uint64_t exception_addr) {
  // x86 returns the instruction *about* to be executed when hitting the hw
  // breakpoint.
  return exception_addr;
}

debug_ipc::NotifyException::Type ArchProvider::DecodeExceptionType(
    const DebuggedThread& thread, uint32_t exception_type) {
  if (exception_type == ZX_EXCP_SW_BREAKPOINT) {
    return debug_ipc::NotifyException::Type::kSoftware;
  } else if (exception_type == ZX_EXCP_HW_BREAKPOINT) {
    zx_thread_state_debug_regs_t debug_regs;
    zx_status_t status = thread.thread().read_state(
        ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs));

    // Assume single step when in doubt.
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "Could not access debug registers for thread "
                       << thread.koid();
      return debug_ipc::NotifyException::Type::kSingleStep;
    }

    DEBUG_LOG(Archx64) << "Decoding HW exception. "
                       << DR6ToString(debug_regs.dr6);

    // HW breakpoints have priority over single-step.
    if (FLAG_VALUE(debug_regs.dr6, kDR6B0) ||
        FLAG_VALUE(debug_regs.dr6, kDR6B1) ||
        FLAG_VALUE(debug_regs.dr6, kDR6B2) ||
        FLAG_VALUE(debug_regs.dr6, kDR6B3)) {
      return debug_ipc::NotifyException::Type::kHardware;
    } else if (FLAG_VALUE(debug_regs.dr6, kDR6BS)) {
      return debug_ipc::NotifyException::Type::kSingleStep;
    } else {
      FXL_NOTREACHED() << "x86: No known hw exception set in DR6";
    }
    return debug_ipc::NotifyException::Type::kSingleStep;
  } else {
    return debug_ipc::NotifyException::Type::kGeneral;
  }
}

zx_status_t ArchProvider::InstallHWBreakpoint(zx::thread* thread,
                                              uint64_t address) {
  FXL_DCHECK(thread);
  // NOTE: Thread needs for the thread to be stopped. Will fail otherwise.
  zx_status_t status;
  zx_thread_state_debug_regs_t debug_regs;
  status = thread->read_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                              sizeof(debug_regs));
  if (status != ZX_OK)
    return status;

  DEBUG_LOG(Archx64) << "Before installing HW breakpoint: " << std::endl
                     << DebugRegistersToString(debug_regs);

  status = SetupHWBreakpoint(address, &debug_regs);
  if (status != ZX_OK)
    return status;

  DEBUG_LOG(Archx64) << "After installing HW breakpoint: " << std::endl
                     << DebugRegistersToString(debug_regs);

  return thread->write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                             sizeof(debug_regs));
}

zx_status_t ArchProvider::UninstallHWBreakpoint(zx::thread* thread,
                                                uint64_t address) {
  FXL_DCHECK(thread);
  // NOTE: Thread needs for the thread to be stopped. Will fail otherwise.
  zx_status_t status;
  zx_thread_state_debug_regs_t debug_regs;
  status = thread->read_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                              sizeof(debug_regs));
  if (status != ZX_OK)
    return status;

  DEBUG_LOG(Archx64) << "Before uninstalling HW breakpoint: " << std::endl
                     << DebugRegistersToString(debug_regs);

  status = RemoveHWBreakpoint(address, &debug_regs);
  if (status != ZX_OK)
    return status;

  DEBUG_LOG(Archx64) << "After uninstalling HW breakpoint: " << std::endl
                     << DebugRegistersToString(debug_regs);

  return thread->write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                             sizeof(debug_regs));
}

zx_status_t ArchProvider::InstallWatchpoint(zx::thread*,
                                            const debug_ipc::AddressRange&) {
  FXL_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ArchProvider::UninstallWatchpoint(zx::thread*,
                                              const debug_ipc::AddressRange&) {
  FXL_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace arch
}  // namespace debug_agent
