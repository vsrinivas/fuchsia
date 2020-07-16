// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_H_

#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch_helpers.h"
#include "src/developer/debug/debug_agent/arch_types.h"
#include "src/developer/debug/ipc/protocol.h"

namespace debug_agent {

class DebuggedThread;
class DebugRegisters;
class ThreadHandle;

namespace arch {

// This file contains architecture-specific low-level helper functions. It is like zircon_utils but
// the functions will have different implementations depending on CPU architecture.
//
// The functions here should be very low-level and are designed for the real (*_zircon.cc)
// implementations of the the various primitives. Cross-platform code should use interfaces like
// ThreadHandle for anything that might need mocking out.

extern const BreakInstructionType kBreakInstruction;

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

// Converts a Zircon exception type to a debug_ipc one. Some exception types require querying the
// thread's debug registers. If needed, the given thread will be used for that.
debug_ipc::ExceptionType DecodeExceptionType(const zx::thread& thread, uint32_t exception_type);

// Class in charge of abstracting the low-level functionalities of the platform.
//
// TODO(brettw) TRhis object is not currently used for any abstractions so we should be able to make
// all functions standalone in the arch namespace.
class ArchProvider {
 public:
  ArchProvider() = default;
  virtual ~ArchProvider() = default;

  // General Exceptions ----------------------------------------------------------------------------

  // Converts an architecture-specific exception record to a cross-platform one.
  static debug_ipc::ExceptionRecord FillExceptionRecord(const zx_exception_report_t& in);

  // Software Exceptions ---------------------------------------------------------------------------

  // Returns the address of the breakpoint instruction given the address of
  // a software breakpoint exception.
  virtual uint64_t BreakpointInstructionForSoftwareExceptionAddress(uint64_t exception_addr);

  // Returns the instruction following the one causing the given software
  // exception.
  uint64_t NextInstructionForSoftwareExceptionAddress(uint64_t exception_addr);

  uint64_t NextInstructionForWatchpointHit(uint64_t exception_addr);

  // Address of the instruction that caused the watchpoint exception. Also returns the slot (which
  // register triggered it).
  //
  // Returns {0, -1} on error or not found.
  virtual std::pair<debug_ipc::AddressRange, int> InstructionForWatchpointHit(
      const DebugRegisters& regs) const;

  // Returns true if there is a breakpoint instruction at the given address.
  // This doesn't just check equality of kBreakInstruction which is guaranteed
  // to be used for our breakpoints, but also checks other encodings that may
  // have been written into the program.
  virtual bool IsBreakpointInstruction(zx::process& process, uint64_t address);

  // Hardware Exceptions ---------------------------------------------------------------------------

  // Returns the address of the instruction that hit the exception from the
  // address reported by the exception.
  uint64_t BreakpointInstructionForHardwareExceptionAddress(uint64_t exception_addr);

 protected:
  uint32_t hw_breakpoint_count_ = 0;
  uint32_t watchpoint_count_ = 0;
};

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_H_
