// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/process.h>
#include <zx/thread.h>

#include "garnet/lib/debug_ipc/protocol.h"

#if defined(__x86_64__)
#include "garnet/bin/debug_agent/arch_x64.h"
#elif defined(__aarch64__)
#include "garnet/bin/debug_agent/arch_arm64.h"
#else
#error
#endif

namespace debug_agent {
namespace arch {

extern const BreakInstructionType kBreakInstruction;

// Class in charge of abstracting the low-level functionalities of the platform.
// This permits a virtual interface for your testing convenience.
class ArchProvider {
 public:
  static ArchProvider& Get();
  // Permits to mock the ArchProvider. Set to nullptr to restore.
  static void Set(ArchProvider*);

  virtual ~ArchProvider();

  // Returns the address of the breakpoint instruction given the address of
  // a software breakpoint exception.
  uint64_t BreakpointInstructionForExceptionAddress(uint64_t exception_addr);

  // Returns the instruction following the one causing the given software
  // exception.
  uint64_t NextInstructionForSoftwareExceptionAddress(uint64_t exception_addr);

  // Returns true if there is a breakpoint instruction at the given address.
  // This doesn't just check equality of kBreakInstruction which is guaranteed
  // to be used for our breakpoints, but also checks other encodings that may
  // have been written into the program.
  bool IsBreakpointInstruction(zx::process& process, uint64_t address);

  // TODO(donosoc): Do we want something like Err here, to communicate what kind
  //                of problems we stumped while doing this?
  bool GetRegisterStateFromCPU(const zx::thread&,
                               std::vector<debug_ipc::RegisterCategory>*);

  // Returns the address of the instruction pointer/stack pointer/base pointer
  // in the given reg structure.
  uint64_t* IPInRegs(zx_thread_state_general_regs* regs);
  uint64_t* SPInRegs(zx_thread_state_general_regs* regs);
  uint64_t* BPInRegs(zx_thread_state_general_regs* regs);

  ::debug_ipc::Arch GetArch();
};

}  // namespace arch
}  // namespace debug_agent
