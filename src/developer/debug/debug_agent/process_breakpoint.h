// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_BREAKPOINT_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_BREAKPOINT_H_

#include <set>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/process_memory_accessor.h"
#include "src/developer/debug/ipc/records.h"
#include "src/lib/fxl/macros.h"

namespace debug_agent {

class Breakpoint;
class HardwareBreakpoint;
class SoftwareBreakpoint;

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

  // When a thread receives a breakpoint exception installed by a process
  // breakpoint, it must check if the breakpoint was indeed intended to apply
  // to it (we can have thread-specific breakpoints).
  bool ShouldHitThread(zx_koid_t thread_koid) const;

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
  //
  // NOTE: From this moment, the breakpoint "takes over" the "run-lifetime" of
  //       the thread. This means that it will suspend and resume it according
  //       to what threads are stepping over it.
  void BeginStepOver(zx_koid_t thread_koid);

  // When a thread has a "current breakpoint" its handling and gets a single
  // step exception, it means that it's done stepping over it and calls this
  // in order to resolve the stepping.
  //
  // NOTE: Even though the thread is done stepping over, the breakpoint still
  //       holds the step "run-lifetime". This is because other threads could
  //       still be stepping over, so the thread cannot be resumed just yet.
  //       The breakpoint will do this once all threads are done stepping over.
  void EndStepOver(zx_koid_t thread_koid);

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

  // Returns true if |thread_koid| is currently stepping over the breakpoint.
  // Passing 0 as argument will ask if *any* thread is currently stepping over.
  bool CurrentlySteppingOver(zx_koid_t thread_koid = 0) const;

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
  // instruction can't be put back until there are no more threads in this map.
  //
  // This could be a simple refcount, but is a set so we can more robustly
  // check for mistakes. CurrentlySteppingOver() checks this list to see if
  // the breakpoint is disabled due to stepping.
  std::set<zx_koid_t> threads_stepping_over_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessBreakpoint);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_BREAKPOINT_H_
