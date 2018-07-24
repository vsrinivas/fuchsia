// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/arch.h"
#include "garnet/lib/debug_ipc/register_desc.h"

namespace debug_agent {
namespace arch {

const BreakInstructionType kBreakInstruction = 0xCC;

uint64_t BreakpointInstructionForExceptionAddress(uint64_t exception_addr) {
  // An X86 exception is 1 byte and a breakpoint exception is triggered with
  // RIP pointing to the following instruction.
  return exception_addr - 1;
}

uint64_t NextInstructionForSoftwareExceptionAddress(uint64_t exception_addr) {
  // Exception address is the one following the instruction that caused it,
  // so nothing needs to be done.
  return exception_addr;
}

bool IsBreakpointInstruction(zx::process& process, uint64_t address) {
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

uint64_t* IPInRegs(zx_thread_state_general_regs* regs) { return &regs->rip; }
uint64_t* SPInRegs(zx_thread_state_general_regs* regs) { return &regs->rsp; }

::debug_ipc::Arch GetArch() { return ::debug_ipc::Arch::kX64; }

namespace {

using debug_ipc::RegisterID;

inline debug_ipc::Register CreateRegister(RegisterID id,
                                          uint32_t length,
                                          const void* val_ptr) {
  debug_ipc::Register reg;
  reg.id = id;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(val_ptr);
  reg.data.assign(ptr, ptr + length);
  return reg;
}

inline zx_status_t ReadGeneralRegs(
    const zx::thread& thread, std::vector<debug_ipc::Register>* registers) {
  // We get the general state registers.
  zx_thread_state_general_regs general_registers;
  zx_status_t status =
      thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &general_registers,
                        sizeof(general_registers));
  if (status != ZX_OK)
    return false;

  registers->push_back(
      CreateRegister(RegisterID::kX64_rax, 8u, &general_registers.rax));
  registers->push_back(
      CreateRegister(RegisterID::kX64_rbx, 8u, &general_registers.rbx));
  registers->push_back(
      CreateRegister(RegisterID::kX64_rcx, 8u, &general_registers.rcx));
  registers->push_back(
      CreateRegister(RegisterID::kX64_rdx, 8u, &general_registers.rdx));
  registers->push_back(
      CreateRegister(RegisterID::kX64_rsi, 8u, &general_registers.rsi));
  registers->push_back(
      CreateRegister(RegisterID::kX64_rdi, 8u, &general_registers.rdi));
  registers->push_back(
      CreateRegister(RegisterID::kX64_rbp, 8u, &general_registers.rbp));
  registers->push_back(
      CreateRegister(RegisterID::kX64_rsp, 8u, &general_registers.rsp));
  registers->push_back(
      CreateRegister(RegisterID::kX64_r8, 8u, &general_registers.r8));
  registers->push_back(
      CreateRegister(RegisterID::kX64_r9, 8u, &general_registers.r9));
  registers->push_back(
      CreateRegister(RegisterID::kX64_r10, 8u, &general_registers.r10));
  registers->push_back(
      CreateRegister(RegisterID::kX64_r11, 8u, &general_registers.r11));
  registers->push_back(
      CreateRegister(RegisterID::kX64_r12, 8u, &general_registers.r12));
  registers->push_back(
      CreateRegister(RegisterID::kX64_r13, 8u, &general_registers.r13));
  registers->push_back(
      CreateRegister(RegisterID::kX64_r14, 8u, &general_registers.r14));
  registers->push_back(
      CreateRegister(RegisterID::kX64_r15, 8u, &general_registers.r15));
  registers->push_back(
      CreateRegister(RegisterID::kX64_rip, 8u, &general_registers.rip));
  registers->push_back(
      CreateRegister(RegisterID::kX64_rflags, 8u, &general_registers.rflags));

  return ZX_OK;
}

}  // namespace

bool GetRegisterStateFromCPU(
    const zx::thread& thread,
    std::vector<debug_ipc::RegisterCategory>* categories) {
  categories->clear();

  categories->push_back({debug_ipc::RegisterCategory::Type::kGeneral, {}});
  auto& general_category = categories->back();
  if (ReadGeneralRegs(thread, &general_category.registers) != ZX_OK) {
    categories->clear();
    return false;
  }
  return true;
}

}  // namespace arch
}  // namespace debug_agent
