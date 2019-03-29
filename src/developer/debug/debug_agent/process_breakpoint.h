// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_PROCESS_BREAKPOINT_H_
#define GARNET_BIN_DEBUG_AGENT_PROCESS_BREAKPOINT_H_

#include <map>

#include "src/lib/fxl/macros.h"
#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/process_memory_accessor.h"
#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

class Breakpoint;

// Represents one breakpoint address in a single process. One Breakpoint object
// can expand to many ProcessBreakpoints across multiple processes and within a
// single one (when a symbolic breakpoint expands to multiple addresses). Also,
// multiple Breakpoint objects can refer to the same ProcessBreakpoint when
// they refer to the same address.
class ProcessBreakpoint {
 public:
  // Given the initial Breakpoint object this corresponds to. Breakpoints
  // can be added or removed later.
  //
  // Call Init() immediately after construction to initialize the parts that
  // can report errors.
  explicit ProcessBreakpoint(Breakpoint* breakpoint,
                             DebuggedProcess* debugged_process,
                             ProcessMemoryAccessor* memory_accessor,
                             uint64_t address);
  ~ProcessBreakpoint();

  // Call immediately after construction. If it returns failure, the breakpoint
  // will not work.
  zx_status_t Init();

  zx_koid_t process_koid() const { return process_->koid(); }
  DebuggedProcess* process() const { return process_; }
  uint64_t address() const { return address_; }

  const std::vector<Breakpoint*> breakpoints() const { return breakpoints_; }

  // Adds or removes breakpoints associated with this process/address.
  // Unregister returns whether there are still any breakpoints referring to
  // this address (false means this is unused and should be deleted).
  zx_status_t RegisterBreakpoint(Breakpoint* breakpoint);
  bool UnregisterBreakpoint(Breakpoint* breakpoint);

  // Writing debug breakpoints changes memory contents. If an unmodified
  // virtual picture of memory is needed, this function will replace the
  // replacement from this breakpoint if it appears in the given block.
  // Otherwise does nothing.
  void FixupMemoryBlock(debug_ipc::MemoryBlock* block);

  // Notification that this breakpoint was just hit. All affected Breakpoints
  // will have their stats updated and placed in the *stats param. This makes
  // a difference whether the exceptions was software or hardware (debug
  // registers) triggered.
  //
  // IMPORTANT: The caller should check the stats and for any breakpoint
  // with "should_delete" set, remove the breakpoints. This can't conveniently
  // be done within this call because it will cause this ProcessBreakpoint
  // object to be deleted from within itself.
  void OnHit(debug_ipc::BreakpointType exception_type,
             std::vector<debug_ipc::BreakpointStats>* hit_breakpoints);

  // Call before single-stepping over a breakpoint. This will remove the
  // breakpoint such that it will be put back when the exception is hit and
  // BreakpointStepHasException() is called.
  //
  // The thread must be put into single-step mode by the caller when this
  // function is called.
  void BeginStepOver(zx_koid_t thread_koid);

  // When a thread has a "current breakpoint" its handling, exceptions will be
  // routed here first. A thread has a current breakpoint when it's either
  // suspended (can not generate exceptions), or when stepping over the
  // breakpoint.
  //
  // This function will return true if the exception was from successfully
  // stepping over this breakpoint. Otherwise, the stepped-over instruction
  // (the one with the breakpoint) caused an exception itself (say, an access
  // violation). In either case, the breakpoint will clean up after itself from
  // a single-step.
  bool BreakpointStepHasException(
      zx_koid_t thread_koid, debug_ipc::NotifyException::Type exception_type);

  bool SoftwareBreakpointInstalled() const;
  bool HardwareBreakpointInstalled() const;

 private:
  // A breakpoint could be removed in the middle of single-stepping it. We
  // need to track this to handle the race between deleting it and the
  // step actually happening.
  enum class StepStatus {
    kCurrent,  // Single-step currently valid.
    kObsolete  // Breakpoint was removed while single-stepping over.
  };

  // Returns true if the breakpoint is temporarily disabled as one or more
  // threads step over it.
  bool CurrentlySteppingOver() const;

  // Install or uninstall this breakpoint.
  zx_status_t Update();  // Will add/remove breakpoints as needed/
  void Uninstall();

  DebuggedProcess* process_;                // Not-owning.
  ProcessMemoryAccessor* memory_accessor_;  // Non-owning.

  uint64_t address_;

  // Low-level implementations of the breakpoints.
  // A ProcessBreakpoint represents the actual "installation" of a Breakpoint
  // in a particular location (address). A Breakpoint can have many locations:
  //
  // b Foo() -> If Foo() is inlined, you can get 2+ locations.
  //
  // In that base, that Breakpoint will have two locations, which means two
  // "installations", or ProcessBreakpoint.
  //
  // A Breakpoint can be a software or a hardware one. That will define what
  // kind of installation the ProcessBreakpoint implements. Now, if a software
  // and a separate hardware breakpoint install to the same memory address, they
  // will implement the same ProcessBreakpoint, which will have both
  // |software_breakpoint_| and |hardware_breakpoint_| members instanced.
  // Null means that that particular installation is not used.
  class SoftwareBreakpoint;
  class HardwareBreakpoint;
  std::unique_ptr<SoftwareBreakpoint> software_breakpoint_;
  std::unique_ptr<HardwareBreakpoint> hardware_breakpoint_;

  // Breakpoints that refer to this ProcessBreakpoint. More than one Breakpoint
  // can refer to the same memory address.
  std::vector<Breakpoint*> breakpoints_;

  // Tracks the threads currently single-stepping over this breakpoint.
  // Normally this will be empty (nobody) or have one thread, but could be more
  // than one in rare cases. Maps thread koid to status.
  //
  // A step is executed by putting back the original instruction, stepping the
  // thread, and then re-inserting the breakpoint instruction. The breakpoint
  // instruction can't be put back until there are no more "kCurrent" threads
  // in this map.
  //
  // This could be a simple refcount, but is a set so we can more robustly
  // check for mistakes. CurrentlySteppingOver() checks this list to see if
  // the breakpoint is disabled due to stepping.
  //
  // TODO(brettw) disabling the breakpoint opens a window where another thread
  // can execute and miss the breakpoint. To avoid this, we need to implement
  // something similar to GDB's "displaced step" to execute the instruction
  // without ever removing the breakpoint instruction.
  std::map<zx_koid_t, StepStatus> thread_step_over_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessBreakpoint);
};

}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_PROCESS_BREAKPOINT_H_
