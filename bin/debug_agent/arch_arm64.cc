// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/arch.h"
#include "garnet/lib/debug_ipc/register_desc.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace debug_agent {
namespace arch {

// "BRK 0" instruction.
// - Low 5 bits = 0.
// - High 11 bits = 11010100001
// - In between 16 bits is the argument to the BRK instruction (in this case
//   zero).
const BreakInstructionType kBreakInstruction = 0xd4200000;

uint64_t BreakpointInstructionForExceptionAddress(uint64_t exception_addr) {
  // ARM reports the exception for the exception instruction itself.
  return exception_addr;
}

uint64_t NextInstructionForSoftwareExceptionAddress(uint64_t exception_addr) {
  // For software exceptions, the exception address is the one that caused it,
  // so next one is just 4 bytes following.
  //
  // TODO(brettw) handle THUMB. When a software breakpoint is hit, ESR_EL1
  // will contain the "instruction length" field which for T32 instructions
  // will be 0 (indicating 16-bits). This exception state somehow needs to be
  // plumbed down to our exception handler.
  return exception_addr + 4;
}

bool IsBreakpointInstruction(zx::process& process, uint64_t address) {
  BreakInstructionType data;
  size_t actual_read = 0;
  if (process.read_memory(address, &data, sizeof(BreakInstructionType),
                          &actual_read) != ZX_OK ||
      actual_read != sizeof(BreakInstructionType))
    return false;

  // The BRK instruction could have any number associated with it, even though
  // we only write "BRK 0", so check for the low 5 and high 11 bytes as
  // described above.
  constexpr BreakInstructionType kMask = 0b11111111111000000000000000011111;
  return (data & kMask) == kBreakInstruction;
}

uint64_t* IPInRegs(zx_thread_state_general_regs* regs) { return &regs->pc; }
uint64_t* SPInRegs(zx_thread_state_general_regs* regs) { return &regs->sp; }

::debug_ipc::Arch GetArch() { return ::debug_ipc::Arch::kArm64; }

namespace {

using debug_ipc::RegisterID;

inline debug_ipc::Register CreateRegister(RegisterID id,
                                          uint32_t length,
                                          const void* val_ptr) {
  debug_ipc::Register reg;
  reg.id = id;
  reg.data.reserve(length);
  memcpy(&reg.data[0], val_ptr, length);
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

  // We add the X0-X29 registers.
  uint32_t base = static_cast<uint32_t>(RegisterID::ARMv8_x0);
  for (int i = 0; i < 30; i++) {
    RegisterID type = static_cast<RegisterID>(base + i);
    registers->push_back(CreateRegister(type, 8u, &general_registers.r[i]));
  }

  // Add the named registers.
  registers->push_back(
      CreateRegister(RegisterID::ARMv8_lr, 8u, &general_registers.lr));
  registers->push_back(
      CreateRegister(RegisterID::ARMv8_sp, 8u, &general_registers.sp));
  registers->push_back(
      CreateRegister(RegisterID::ARMv8_pc, 8u, &general_registers.pc));
  registers->push_back(
      CreateRegister(RegisterID::ARMv8_cpsr, 8u, &general_registers.cpsr));

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
