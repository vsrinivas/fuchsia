// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/arch.h"

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

bool GetRegisterStateFromCPU(const zx::thread& thread,
                             std::vector<debug_ipc::Register>* registers) {
  registers->clear();

  // We get the general state registers
  zx_thread_state_general_regs general_registers;
  zx_status_t status =
      thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &general_registers,
                        sizeof(general_registers));
  if (status != ZX_OK)
    return false;

  registers->push_back({"RAX", general_registers.rax});
  registers->push_back({"RBX", general_registers.rbx});
  registers->push_back({"RCX", general_registers.rcx});
  registers->push_back({"RDX", general_registers.rdx});
  registers->push_back({"RSI", general_registers.rsi});
  registers->push_back({"RDI", general_registers.rdi});
  registers->push_back({"RBP", general_registers.rbp});
  registers->push_back({"RSP", general_registers.rsp});
  registers->push_back({"R8", general_registers.r8});
  registers->push_back({"R9", general_registers.r9});
  registers->push_back({"R10", general_registers.r10});
  registers->push_back({"R11", general_registers.r11});
  registers->push_back({"R12", general_registers.r12});
  registers->push_back({"R13", general_registers.r13});
  registers->push_back({"R14", general_registers.r14});
  registers->push_back({"R15", general_registers.r15});
  registers->push_back({"RIP", general_registers.rip});
  registers->push_back({"RFLAGS", general_registers.rflags});
  return false;
}

}  // namespace arch
}  // namespace debug_agent
