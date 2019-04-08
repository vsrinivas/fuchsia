// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_ARCH_H_
#define GARNET_BIN_DEBUG_AGENT_ARCH_H_

#include <lib/zx/process.h>
#include <lib/zx/thread.h>

#include "src/developer/debug/ipc/protocol.h"

#if defined(__x86_64__)
#include "src/developer/debug/debug_agent/arch_x64.h"
#elif defined(__aarch64__)
#include "src/developer/debug/debug_agent/arch_arm64.h"
#else
#error
#endif

namespace debug_agent {

class DebuggedThread;

namespace arch {

extern const BreakInstructionType kBreakInstruction;

// Class in charge of abstracting the low-level functionalities of the platform.
// This permits a virtual interface for your testing convenience.
class ArchProvider {
 public:
  static ArchProvider& Get();
  // Permits to mock the ArchProvider. Set to nullptr to restore.
  static void Set(std::unique_ptr<ArchProvider>);

  virtual ~ArchProvider();

  ::debug_ipc::Arch GetArch();

  // Returns the address of the instruction pointer/stack pointer/base pointer
  // in the given reg structure.
  uint64_t* IPInRegs(zx_thread_state_general_regs* regs);
  uint64_t* SPInRegs(zx_thread_state_general_regs* regs);
  uint64_t* BPInRegs(zx_thread_state_general_regs* regs);

  // Software Exceptions -------------------------------------------------------

  // Returns the address of the breakpoint instruction given the address of
  // a software breakpoint exception.
  uint64_t BreakpointInstructionForSoftwareExceptionAddress(
      uint64_t exception_addr);

  // Returns the instruction following the one causing the given software
  // exception.
  uint64_t NextInstructionForSoftwareExceptionAddress(uint64_t exception_addr);

  // Returns true if there is a breakpoint instruction at the given address.
  // This doesn't just check equality of kBreakInstruction which is guaranteed
  // to be used for our breakpoints, but also checks other encodings that may
  // have been written into the program.
  bool IsBreakpointInstruction(zx::process& process, uint64_t address);

  virtual zx_status_t ReadRegisters(
      const debug_ipc::RegisterCategory::Type& cat, const zx::thread&,
      std::vector<debug_ipc::Register>* out);

  // The RegisterCategory will have the corresponding register values, which
  // the arch will make sure it writes correctly. Since each category is a
  // different syscall anyway, there is no need to group many categories into
  // one call.
  virtual zx_status_t WriteRegisters(const debug_ipc::RegisterCategory&,
                                     zx::thread*);

  // Hardware Exceptions -------------------------------------------------------

  // Returns the address of the instruction that hit the exception from the
  // address reported by the exception.
  uint64_t BreakpointInstructionForHardwareExceptionAddress(
      uint64_t exception_addr);

  // Currently HW notifications can mean both a single step or a hardware debug
  // register exception. We need platform-specific queries to figure which one
  // is it.
  debug_ipc::NotifyException::Type DecodeExceptionType(const DebuggedThread&,
                                                       uint32_t exception_type);

  // TODO: Support watchpoints.
  virtual zx_status_t InstallHWBreakpoint(zx::thread*, uint64_t address);
  virtual zx_status_t UninstallHWBreakpoint(zx::thread*, uint64_t address);

  virtual zx_status_t InstallWatchpoint(zx::thread*,
                                        const debug_ipc::AddressRange&);
  virtual zx_status_t UninstallWatchpoint(zx::thread*,
                                          const debug_ipc::AddressRange&);
};

}  // namespace arch
}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_ARCH_H_
