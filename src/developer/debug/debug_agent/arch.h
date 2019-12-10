// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_H_

#include <lib/zx/process.h>
#include <lib/zx/thread.h>

#include "src/developer/debug/debug_agent/arch_helpers.h"
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
  ArchProvider();
  virtual ~ArchProvider();

  ::debug_ipc::Arch GetArch();

  uint32_t hw_breakpoint_count() const { return hw_breakpoint_count_; }
  void set_hw_breakpoint_count(uint32_t count) { hw_breakpoint_count_ = count; }

  uint32_t watchpoint_count() const { return watchpoint_count_; }
  void set_watchpoint_count(uint32_t count) { watchpoint_count_ = count; }

  // Thread Management -----------------------------------------------------------------------------

  // zx_thread_read_state with ZX_THREAD_STATE_GENERAL_REGS.
  virtual zx_status_t ReadGeneralState(const zx::thread& handle,
                                       zx_thread_state_general_regs* regs);

  virtual zx_status_t ReadDebugState(const zx::thread& handle, zx_thread_state_debug_regs* regs);

  // zx_thread_write_state with ZX_THREAD_STATE_GENERAL_REGS.
  virtual zx_status_t WriteGeneralState(const zx::thread& handle,
                                        const zx_thread_state_general_regs& regs);

  virtual zx_status_t WriteSingleStep(const zx::thread& thread, bool single_step);

  virtual zx_status_t WriteDebugState(const zx::thread& handle,
                                      const zx_thread_state_debug_regs& regs);

  // Returns the address of the instruction pointer/stack pointer/base pointer
  // in the given reg structure.
  virtual uint64_t* IPInRegs(zx_thread_state_general_regs* regs);
  virtual uint64_t* SPInRegs(zx_thread_state_general_regs* regs);
  virtual uint64_t* BPInRegs(zx_thread_state_general_regs* regs);

  // zx_object_get_info.
  virtual zx_status_t GetInfo(const zx::thread&, zx_object_info_topic_t topic, void* buffer,
                              size_t buffer_size, size_t* actual = nullptr,
                              size_t* avail = nullptr);

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
  virtual std::pair<uint64_t, int> InstructionForWatchpointHit(const DebuggedThread&) const;

  // Returns true if there is a breakpoint instruction at the given address.
  // This doesn't just check equality of kBreakInstruction which is guaranteed
  // to be used for our breakpoints, but also checks other encodings that may
  // have been written into the program.
  virtual bool IsBreakpointInstruction(zx::process& process, uint64_t address);

  // Converts the given register structure to a vector of debug_ipc registers.
  static void SaveGeneralRegs(const zx_thread_state_general_regs& input,
                              std::vector<debug_ipc::Register>* out);

  // The registers in the given category are appended to the given output vector.
  virtual zx_status_t ReadRegisters(const debug_ipc::RegisterCategory& cat, const zx::thread&,
                                    std::vector<debug_ipc::Register>* out);

  // The registers must all be in the same category.
  virtual zx_status_t WriteRegisters(const debug_ipc::RegisterCategory&,
                                     const std::vector<debug_ipc::Register>& registers,
                                     zx::thread*);

  // Hardware Exceptions ---------------------------------------------------------------------------

  // Returns the address of the instruction that hit the exception from the
  // address reported by the exception.
  uint64_t BreakpointInstructionForHardwareExceptionAddress(uint64_t exception_addr);

  // Currently HW notifications can mean both a single step or a hardware debug
  // register exception. We need platform-specific queries to figure which one
  // is it.
  virtual debug_ipc::ExceptionType DecodeExceptionType(const DebuggedThread&,
                                                       uint32_t exception_type);

  virtual zx_status_t InstallHWBreakpoint(const zx::thread&, uint64_t address);
  virtual zx_status_t UninstallHWBreakpoint(const zx::thread&, uint64_t address);

  virtual WatchpointInstallationResult InstallWatchpoint(const zx::thread&,
                                                         const debug_ipc::AddressRange&);
  virtual zx_status_t UninstallWatchpoint(const zx::thread&, const debug_ipc::AddressRange&);

 protected:
  uint32_t hw_breakpoint_count_ = 0;
  uint32_t watchpoint_count_ = 0;
};

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_H_
