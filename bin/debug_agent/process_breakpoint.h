// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/debug_agent/arch.h"
#include "garnet/lib/debug_ipc/records.h"
#include "garnet/public/lib/fxl/macros.h"

class DebuggedProcess;
class DebuggedThread;

// Represents a breakpoint in the debug agent. These breakpoints are
// per-process (client breakpoints in the zxdb debugger can span multiple
// processes, and in that case will reference multiple ProcessBreakpoints).
class ProcessBreakpoint {
 public:
  // Call SetSettings immediately after construction.
  explicit ProcessBreakpoint(DebuggedProcess* process);
  ~ProcessBreakpoint();

  uint64_t address() const { return settings_.address; }

  // If SetSettings fails, this breakpoint is invalid and should be deleted.
  bool SetSettings(const debug_ipc::BreakpointSettings& settings);

  // Writing debug breakpoints changes memory contents. If an unmodified
  // virtual picture of memory is needed, this function will replace the
  // replacement from this breakpoint if it appears in the given block.
  // Otherwise does nothing.
  void FixupMemoryBlock(debug_ipc::MemoryBlock* block);

  // Call before single-stepping over a breakpoint. This will remove the
  // breakpoint such that it will be put back when the exception is hit and
  // BreakpointStepHasException() is called.
  //
  // The thread must be put into single-step mode by the caller when this
  // function is called.
  void BeginStepOver(DebuggedThread* thread);

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
  bool BreakpointStepHasException(DebuggedThread* thread,
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
  bool Install();
  void Uninstall();

  DebuggedProcess* process_;  // Backpointer to owner of this class.

  debug_ipc::BreakpointSettings settings_;

  // Set to true when the instruction has been replaced.
  bool installed_ = false;

  // Previous memory contents before being replaced with the break instruction.
  arch::BreakInstructionType previous_data_ = 0;

  // Tracks the threads currently single-stepping over this breakpoint.
  // Normally this will be empty (nobody) or have one thread, but could be more
  // than one in rare cases.
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
  std::map<DebuggedThread*, StepStatus> thread_step_over_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessBreakpoint);
};
