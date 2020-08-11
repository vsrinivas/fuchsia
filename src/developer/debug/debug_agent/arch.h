// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_H_

#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch_types.h"
#include "src/developer/debug/ipc/protocol.h"

namespace debug_agent {

class DebuggedThread;
class ThreadHandle;

namespace arch {

// This file contains architecture-specific low-level helper functions. It is like zircon_utils but
// the functions will have different implementations depending on CPU architecture.
//
// The functions here should be very low-level and are designed for the real (*_zircon.cc)
// implementations of the the various primitives. Cross-platform code should use interfaces like
// ThreadHandle for anything that might need mocking out.

// Our canonical breakpoint instruction for the current architecture. This is what we'll write for
// software breakpoints. Some platforms may have alternate encodings for software breakpoints, so to
// check if something is a breakpoint instruction, use arch::IsBreakpointInstruction() rather than
// checking for equality with this value.
extern const BreakInstructionType kBreakInstruction;

// Distance offset from a software breakpoint instruction that the exception will be reported as
// thron at. Architectures differ about what address is reported for a software breakpoint
// exception.
//
//  * To convert from a software breakpoint address to the exception address, add this.
//  * To convert from an exception address to the breakpoint instruction address, subtract this.
extern const int64_t kExceptionOffsetForSoftwareBreakpoint;

debug_ipc::Arch GetCurrentArch();

// Returns the number of hardware breakpoints and watchpoints on the current system.
uint32_t GetHardwareBreakpointCount();
uint32_t GetHardwareWatchpointCount();

// Converts the given register structure to a vector of debug_ipc registers.
void SaveGeneralRegs(const zx_thread_state_general_regs& input,
                     std::vector<debug_ipc::Register>& out);

// The registers in the given category are appended to the given output vector.
zx_status_t ReadRegisters(const zx::thread& thread, const debug_ipc::RegisterCategory& cat,
                          std::vector<debug_ipc::Register>& out);

// The registers must all be in the same category.
zx_status_t WriteRegisters(zx::thread& thread, const debug_ipc::RegisterCategory& cat,
                           const std::vector<debug_ipc::Register>& registers);

// Given the current register value in |regs|, applies to it the new updated values for the
// registers listed in |updates|.
zx_status_t WriteGeneralRegisters(const std::vector<debug_ipc::Register>& updates,
                                  zx_thread_state_general_regs_t* regs);
zx_status_t WriteFloatingPointRegisters(const std::vector<debug_ipc::Register>& update,
                                        zx_thread_state_fp_regs_t* regs);
zx_status_t WriteVectorRegisters(const std::vector<debug_ipc::Register>& update,
                                 zx_thread_state_vector_regs_t* regs);
zx_status_t WriteDebugRegisters(const std::vector<debug_ipc::Register>& update,
                                zx_thread_state_debug_regs_t* regs);

// Writes the register data to the given output variable, checking that the register data is
// the same size as the output.
template <typename RegType>
zx_status_t WriteRegisterValue(const debug_ipc::Register& reg, RegType* dest) {
  if (reg.data.size() != sizeof(RegType))
    return ZX_ERR_INVALID_ARGS;
  memcpy(dest, reg.data.data(), sizeof(RegType));
  return ZX_OK;
}

// Converts a Zircon exception type to a debug_ipc one. Some exception types require querying the
// thread's debug registers. If needed, the given thread will be used for that.
debug_ipc::ExceptionType DecodeExceptionType(const zx::thread& thread, uint32_t exception_type);

// Converts an architecture-specific exception record to a cross-platform one.
debug_ipc::ExceptionRecord FillExceptionRecord(const zx_exception_report_t& in);

// Returns the instruction following the one causing the given software exception.
uint64_t NextInstructionForSoftwareExceptionAddress(uint64_t exception_addr);

// Returns true if the given opcode is a breakpoint instruction. This checked for equality with
// kBreakInstruction and also checks other possible breakpoint encodings for the current platform.
bool IsBreakpointInstruction(BreakInstructionType instruction);

// Returns the address of the instruction that hit the exception from the address reported by the
// exception.
uint64_t BreakpointInstructionForHardwareExceptionAddress(uint64_t exception_addr);

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_H_
