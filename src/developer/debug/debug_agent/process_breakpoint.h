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
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"

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
  explicit ProcessBreakpoint(Breakpoint* breakpoint, DebuggedProcess* debugged_process,
                             ProcessMemoryAccessor* memory_accessor, uint64_t address);
  ~ProcessBreakpoint();

  // Call immediately after construction. If it returns failure, the breakpoint
  // will not work.
  zx_status_t Init();

  fxl::WeakPtr<ProcessBreakpoint> GetWeakPtr();

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

  // Call before single-stepping over a breakpoint. This will remove the breakpoint such that it
  // will be put back when the exception is hit and BreakpointStepHasException() is called.
  //
  // This will not execute the stepping over directly, but rather enqueue it within the process so
  // that each stepping over is done one at a time.
  //
  // The actual stepping over logic is done by |ExecuteStepOver|, which is called by the process.
  //
  // NOTE: From this moment, the breakpoint "takes over" the "run-lifetime" of
  //       the thread. This means that it will suspend and resume it according
  //       to what threads are stepping over it.
  void BeginStepOver(DebuggedThread* thread);

  // When a thread has a "current breakpoint" its handling and gets a single
  // step exception, it means that it's done stepping over it and calls this
  // in order to resolve the stepping.
  //
  // This will tell the process that this stepping over instance is done and will call
  // |OnBreakpointFinishedSteppingOver|, which will advance the queue so that the other queued
  // step overs can occur.
  //
  // NOTE: Even though the thread is done stepping over, this will *not* resume the suspended
  //       threads nor the excepted (stepping over) thread. This is done on |StepOverCleanup|.
  //       This is because there might be another breakpoint queued up and that breakpoint needs a
  //       chance to suspend the threads before these are unsuspended from the previous breakpoint.
  //
  //       Otherwise we introduce a race between the current step over breakpoint resuming the
  //       threads and the next one suspending them.
  //
  //       With the new order, the process will first call the next process |ExecuteStepOver|, which
  //       will suspend the corresponding threads and then |StepOverCleanup| will free the
  //       threads suspended by the current one.
  void EndStepOver(DebuggedThread* thread);

  // Called by the queue-owning process.
  //
  // This function actually sets up the stepping over and suspend *all* other threads.
  // When the thread is done stepping over, it will call the process
  //|OnBreakpointFinishedSteppingOver| function.
  void ExecuteStepOver(DebuggedThread* thread);

  // Frees all the suspension and exception resources held by the breakpoint. This is called by the
  // process.
  //
  // See the comments of |EndStepOver| for more details.
  void StepOverCleanup(DebuggedThread* thread);

  // As stepping over are queued, only one thread should be left running at a time. This makes the
  // breakpoint get a suspend token for each other thread within the system.
  void SuspendAllOtherThreads(zx_koid_t stepping_over_koid);

  bool SoftwareBreakpointInstalled() const;
  bool HardwareBreakpointInstalled() const;

  const DebuggedThread* currently_stepping_over_thread() const {
    return currently_stepping_over_thread_.get();
  }

  // Returns a sorted list of the koids associated with a currently held suspend token.
  // If a thread has more than one suspend token, it wil appear twice.
  //
  // Exposed mostly for testing purposes (see process_breakpoint_unittest.cc).
  std::vector<zx_koid_t> CurrentlySuspendedThreads() const;

 private:
  // A breakpoint could be removed in the middle of single-stepping it. We
  // need to track this to handle the race between deleting it and the
  // step actually happening.
  enum class StepStatus {
    kCurrent,  // Single-step currently valid.
    kObsolete  // Breakpoint was removed while single-stepping over.
  };

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
  // There can be only one thread stepping over, as they're serialized by the process so that only
  // one thread is stepping at a time.
  fxl::WeakPtr<DebuggedThread> currently_stepping_over_thread_;

  // A step is executed by putting back the original instruction, stepping the thread, and then
  // re-inserting the breakpoint instruction. The breakpoint instruction can't be put back until
  // there are no more threads in this map.
  //
  // It is a multimap because if two threads are queued on the same breakpoint (they both hit it at
  // the same time), the breakpoint will get suspend tokens for all the threads (except the
  // corresponding exception one) multiple times. If there is only one suspend token per koid, the
  // breakpoint will uncorrectly resume the thread that just stepped over when the other would
  // step over too, which is incorrect. We need the ability to have multiple tokens associated to
  // a thread so that the interim between executing the second step over the same breakpoint can
  // coincide with waiting for the resources of the first step over to be freed.
  //
  // See the implementation of |StepOverCleanup| for more details.
  std::multimap<zx_koid_t, std::unique_ptr<DebuggedThread::SuspendToken>> suspend_tokens_;

  fxl::WeakPtrFactory<ProcessBreakpoint> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessBreakpoint);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_BREAKPOINT_H_
