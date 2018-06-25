// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_PROCESS_BREAKPOINT_H_
#define GARNET_BIN_DEBUG_AGENT_PROCESS_BREAKPOINT_H_

#include <map>

#include "garnet/bin/debug_agent/arch.h"
#include "garnet/bin/debug_agent/process_memory_accessor.h"
#include "garnet/lib/debug_ipc/records.h"
#include "lib/fxl/macros.h"

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
  // Call Init() immediately after construction to initalize the parts that
  // can report errors.
  explicit ProcessBreakpoint(Breakpoint* breakpoint,
                             ProcessMemoryAccessor* memory_accessor,
                             zx_koid_t process_koid, uint64_t address);
  ~ProcessBreakpoint();

  // Call immediately after construction. If it returns failure, the breakpoint
  // will not work.
  zx_status_t Init();

  zx_koid_t process_koid() const { return process_koid_; }
  uint64_t address() const { return address_; }

  // Adds or removes breakpoints associated with this process/address.
  // Unregister returns whether there are still any breakpoints referring to
  // this address (false means this is unused and should be deleted).
  void RegisterBreakpoint(Breakpoint* breakpoint);
  bool UnregisterBreakpoint(Breakpoint* breakpoint);

  // Writing debug breakpoints changes memory contents. If an unmodified
  // virtual picture of memory is needed, this function will replace the
  // replacement from this breakpoint if it appears in the given block.
  // Otherwise does nothing.
  void FixupMemoryBlock(debug_ipc::MemoryBlock* block);

  // Notification that this breakpoint was just hit. All affected Breakpoints
  // will have their stats updated and placed in the *stats param.
  //
  // IMPORTANT: The caller should check the stats and for any breakpoint
  // with "should_delete" set, remove the breakpoints. This can't conveniently
  // be done within this call because it will cause this ProcessBreakpoint
  // object to be deleted from within itself.
  void OnHit(std::vector<debug_ipc::BreakpointStats>* hit_breakpoints);

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
  bool BreakpointStepHasException(zx_koid_t thread_koid,
                                  uint32_t exception_type);

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
  zx_status_t Install();
  void Uninstall();

  ProcessMemoryAccessor* memory_accessor_;  // Non-owning.

  zx_koid_t process_koid_;
  uint64_t address_;

  // Set to true when the instruction has been replaced.
  bool installed_ = false;

  // Previous memory contents before being replaced with the break instruction.
  arch::BreakInstructionType previous_data_ = 0;

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
