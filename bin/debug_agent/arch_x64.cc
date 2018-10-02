// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/arch.h"

#include <zircon/syscalls/exception.h>

#include "garnet/bin/debug_agent/debugged_thread.h"
#include "garnet/lib/debug_ipc/register_desc.h"
#include "lib/fxl/logging.h"

namespace debug_agent {
namespace arch {

#ifdef ZIRCON_DEBUG_REG_PATCH_LANDED

constexpr uint64_t kDR6B0 = (1 << 0);
constexpr uint64_t kDR6B1 = (1 << 1);
constexpr uint64_t kDR6B2 = (1 << 2);
constexpr uint64_t kDR6B3 = (1 << 3);
constexpr uint64_t kDR6BD = (1 << 13);
constexpr uint64_t kDR6BS = (1 << 14);
constexpr uint64_t kDR6BT = (1 << 15);

constexpr uint64_t kDR7L0 = (1 << 0);
constexpr uint64_t kDR7G0 = (1 << 1);
constexpr uint64_t kDR7L1 = (1 << 2);
constexpr uint64_t kDR7G1 = (1 << 3);
constexpr uint64_t kDR7L2 = (1 << 4);
constexpr uint64_t kDR7G2 = (1 << 5);
constexpr uint64_t kDR7L3 = (1 << 6);
constexpr uint64_t kDR7G3 = (1 << 7);
constexpr uint64_t kDR7LE = (1 << 8);
constexpr uint64_t kDR7GE = (1 << 9);
constexpr uint64_t kDR7GD = (1 << 13);
constexpr uint64_t kDR7RW0 = (1 << 16);
constexpr uint64_t kDR7LEN0 = (1 << 18);
constexpr uint64_t kDR7RW1  = (1 << 20);
constexpr uint64_t kDR7LEN1 = (1 << 22);
constexpr uint64_t kDR7RW2  = (1 << 24);
constexpr uint64_t kDR7LEN2 = (1 << 26);
constexpr uint64_t kDR7RW3  = (1 << 28);
constexpr uint64_t kDR7LEN3 = (1 << 30);

#endif

#define FLAG_VALUE(val, shift) ((val) & (shift))

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
#ifdef ZIRCON_DEBUG_REG_PATCH_LANDED
  out->push_back(CreateRegister(RegisterID::kX64_dr6, 8u, &debug_regs.dr6));
  out->push_back(CreateRegister(RegisterID::kX64_dr7, 8u, &debug_regs.dr7));
#else
  out->push_back(
      CreateRegister(RegisterID::kX64_dr6, 8u, &debug_regs.dr6_status));
  out->push_back(
      CreateRegister(RegisterID::kX64_dr7, 8u, &debug_regs.dr7_control));
#endif
  return ZX_OK;
}

}  // namespace

bool ArchProvider::GetRegisterStateFromCPU(
    const zx::thread& thread, std::vector<debug_ipc::RegisterCategory>* cats) {
  cats->clear();

  cats->push_back({debug_ipc::RegisterCategory::Type::kGeneral, {}});
  auto& general_category = cats->back();
  if (ReadGeneralRegs(thread, &general_category.registers) != ZX_OK) {
    cats->clear();
    return false;
  }

  cats->push_back({debug_ipc::RegisterCategory::Type::kFloatingPoint, {}});
  auto& fp_category = cats->back();
  if (ReadFPRegs(thread, &fp_category.registers) != ZX_OK) {
    cats->clear();
    return false;
  }

  cats->push_back({debug_ipc::RegisterCategory::Type::kVector, {}});
  auto& vec_category = cats->back();
  if (ReadVectorRegs(thread, &vec_category.registers) != ZX_OK) {
    cats->clear();
    return false;
  }

  cats->push_back({debug_ipc::RegisterCategory::Type::kDebug, {}});
  auto& debug_category = cats->back();
  if (ReadDebugRegs(thread, &debug_category.registers) != ZX_OK) {
    cats->clear();
    return false;
  }

  return true;
}

// Hardware Exceptions ---------------------------------------------------------

uint64_t ArchProvider::BreakpointInstructionForHardwareExceptionAddress(
    uint64_t exception_addr) {
  // x86 returns the instruction *about* to be executed when hitting the hw
  // breakpoint.
  return exception_addr;
}

uint64_t ArchProvider::NextInstructionForHardwareExceptionAddress(
    uint64_t exception_addr) {
  // Exception address is the one following the instruction that caused it,
  // so nothing needs to be done.
  return exception_addr;
}

debug_ipc::NotifyException::Type ArchProvider::DecodeExceptionType(
    const DebuggedThread& thread, uint32_t exception_type) {
  if (exception_type == ZX_EXCP_SW_BREAKPOINT) {
    return debug_ipc::NotifyException::Type::kSoftware;
  } else if (exception_type == ZX_EXCP_HW_BREAKPOINT) {
#ifdef ZIRCON_DEBUG_REG_PATCH_LANDED
    zx_thread_state_debug_regs_t debug_regs;
    zx_status_t status = thread.thread().read_state(
        ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs));

    // Assume single step when in doubt.
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "Could not access debug registers for thread "
                       << thread.koid();
      return debug_ipc::NotifyException::Type::kSingleStep;
    }

    if (FLAG_VALUE(debug_regs.dr6, kDR6BS)) {
      return debug_ipc::NotifyException::Type::kSingleStep;
    } else if (FLAG_VALUE(debug_regs.dr6, kDR6B0) ||
               FLAG_VALUE(debug_regs.dr6, kDR6B1) ||
               FLAG_VALUE(debug_regs.dr6, kDR6B2) ||
               FLAG_VALUE(debug_regs.dr6, kDR6B3)) {
      return debug_ipc::NotifyException::Type::kHardware;
    } else {
      FXL_NOTREACHED() << "x86: No known hw exception set in DR6";
    }
    return debug_ipc::NotifyException::Type::kSingleStep;
#else
    // Until the patch lands, HW exception == single step.
    return debug_ipc::NotifyException::Type::kSingleStep;
#endif
  } else {
    return debug_ipc::NotifyException::Type::kGeneral;
  }
}

#ifdef ZIRCON_DEBUG_REG_PATCH_LANDED

namespace {

struct DebugRegShift {
  int index = -1;
  uint64_t bp_shift = 0;    // Enable shift within DR7
  uint64_t rw_shift = 0;    // RW shift within DR7
  uint64_t len_shift = 0;   // LEN shift within DR7
};

const DebugRegShift* GetDebugRegisterShifts(size_t index) {
  static std::vector<DebugRegShift> shifts = {
      {0, kDR7L0, kDR7RW0, kDR7LEN0},
      {1, kDR7L1, kDR7RW1, kDR7LEN1},
      {2, kDR7L2, kDR7RW2, kDR7LEN2},
      {3, kDR7L3, kDR7RW3, kDR7LEN3},
  };
  if (index >= shifts.size())
    return nullptr;
  return &shifts[index];
}

}  // namespace

#endif

zx_status_t ArchProvider::InstallHWBreakpoint(zx::thread* thread,
                                              uint64_t address) {
#ifdef ZIRCON_DEBUG_REG_PATCH_LANDED
  zx_thread_state_debug_regs_t debug_regs;
  zx_status_t status = thread->read_state(ZX_THREAD_STATE_DEBUG_REGS,
                                          &debug_regs, sizeof(debug_regs));
  if (status != ZX_OK)
    return status;

  // Search for an unset register.
  // TODO: This doesn't check that the supplied register is already set.
  const DebugRegShift* slot = nullptr;
  for (size_t i = 0; i < 4; i++) {
    const DebugRegShift* shift = GetDebugRegisterShifts(i);
    if (!FLAG_VALUE(debug_regs.dr7, shift->bp_shift)) {
      slot = shift;
    }
  }

  if (!slot) {
    FXL_LOG(WARNING) << "No available debug register to set.";
    // No available registers.
    return ZX_ERR_NO_RESOURCES;
  }

  debug_regs.dr[slot->index] = address;
  // Modify the DR7 register.
  // For now only add execution breakpoints.
  uint64_t dr7 = debug_regs.dr7;
  dr7 |= (1 << slot->bp_shift);     // Activate the breakpoint.
  // TODO(donosoc): Handle LEN properties of the breakpoint.
  uint64_t mask = ((uint64_t)0b11) << slot->rw_shift;
  dr7 &= ~mask;
  debug_regs.dr7 = dr7;

  return thread->write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                             sizeof(debug_regs));
#else
  return ZX_ERR_NOT_SUPPORTED;
#endif
}

zx_status_t ArchProvider::UninstallHWBreakpoint(zx::thread* thread,
                                                uint64_t address) {
#ifdef ZIRCON_DEBUG_REG_PATCH_LANDED
  zx_thread_state_debug_regs_t debug_regs;
  zx_status_t status = thread->read_state(ZX_THREAD_STATE_DEBUG_REGS,
                                          &debug_regs, sizeof(debug_regs));
  if (status != ZX_OK)
    return status;

  // Search for the address.
  for (size_t i = 0; i < 4; i++) {
    if (address != debug_regs.dr[i])
      continue;

    const DebugRegShift* shift = GetDebugRegisterShifts(i);
    // Only unset the
    uint64_t dr7 = debug_regs.dr7;
    dr7 &= ~(1 << shift->bp_shift);     // Disable the breakpoint.

    debug_regs.dr[i] = 0;
    debug_regs.dr7 = dr7;

    return thread->write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs,
                               sizeof(debug_regs));
  }

  // Not found. We return ok.
  return ZX_OK;
#else
  return ZX_ERR_NOT_SUPPORTED;
#endif
}

}  // namespace arch
}  // namespace debug_agent
