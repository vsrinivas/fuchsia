// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_BREAKPOINT_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_BREAKPOINT_H_

#include <set>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/status.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_ipc {
struct ThreadRecord;
}

namespace debug_agent {

class Breakpoint;
class HardwareBreakpoint;
class SoftwareBreakpoint;

// Low-level implementations of the breakpoints. A ProcessBreakpoint represents the actual
// "installation" of a Breakpoint in a particular location (address). A Breakpoint can have many
// locations:
//
// b Foo() -> If Foo() is inlined, you can get 2+ locations.
//
// In that base, that Breakpoint will have two locations, which means two "installations", or
// ProcessBreakpoint.
//
// A Breakpoint can be a software or hardware one. That will define what kind of specialization the
// ProcessBreakpoint implements.
class ProcessBreakpoint {
 public:
  // Given the initial Breakpoint object this corresponds to. Breakpoints can be added or removed
  // later.
  //
  // Call Init() immediately after construction to initialize the parts that can report errors.
  explicit ProcessBreakpoint(Breakpoint* breakpoint, DebuggedProcess* debugged_process,
                             uint64_t address);
  virtual ~ProcessBreakpoint();

  virtual debug_ipc::BreakpointType Type() const = 0;

  // Call immediately after construction. If it returns failure, the breakpoint will not work.
  debug::Status Init();

  virtual bool Installed(zx_koid_t thread_koid) const = 0;

  fxl::WeakPtr<ProcessBreakpoint> GetWeakPtr();

  zx_koid_t process_koid() const { return process_->koid(); }
  DebuggedProcess* process() const { return process_; }
  uint64_t address() const { return address_; }

  const std::vector<Breakpoint*>& breakpoints() const { return breakpoints_; }

  // Adds or removes breakpoints associated with this process/address. Unregister returns whether
  // there are still any breakpoints referring to this address (false means this is unused and
  // should be deleted).
  debug::Status RegisterBreakpoint(Breakpoint* breakpoint);
  bool UnregisterBreakpoint(Breakpoint* breakpoint);

  // When a thread receives a breakpoint exception installed by a process breakpoint, it must check
  // if the breakpoint was indeed intended to apply to it (we can have thread-specific breakpoints).
  bool ShouldHitThread(zx_koid_t thread_koid) const;

  // Notification that this breakpoint was just hit. All affected Breakpoints will have their stats
  // updated and placed in the *stats param. This makes a difference whether the exceptions was
  // software or hardware (debug registers) triggered.
  //
  // All threads requested to be suspended (in any process) by this breakpoint's settings will be
  // filled into |other_affected_threads|.
  //
  // IMPORTANT: The caller should check the stats and for any breakpoint with "should_delete" set,
  // remove the breakpoints. This can't conveniently be done within this call because it will cause
  // this ProcessBreakpoint object to be deleted from within itself.
  void OnHit(DebuggedThread* hitting_thread, debug_ipc::BreakpointType exception_type,
             std::vector<debug_ipc::BreakpointStats>& hit_breakpoints,
             std::vector<debug_ipc::ThreadRecord>& other_affected_threads);

  // Call before single-stepping over a breakpoint. This will remove the breakpoint such that it
  // will be put back when the exception is hit and BreakpointStepHasException() is called.
  //
  // This will not execute the stepping over directly, but rather enqueue it within the process so
  // that each stepping over is done one at a time.
  //
  // The actual stepping over logic is done by |ExecuteStepOver|, which is called by the process.
  //
  // NOTE: From this moment, the breakpoint "takes over" the "run-lifetime" of the thread. This
  //       means that it will suspend and resume it according to what threads are stepping over it.
  virtual void BeginStepOver(DebuggedThread* thread);

  // When a thread has a "current breakpoint" its handling and gets a single step exception, it
  // means that it's done stepping over it and calls this in order to resolve the stepping.
  //
  // This will tell the process that this stepping over instance is done and will call
  // |OnBreakpointFinishedSteppingOver|, which will advance the queue so that the other queued step
  // overs can occur.
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
  virtual void EndStepOver(DebuggedThread* thread) = 0;

  // Called by the queue-owning process.
  //
  // This function actually sets up the stepping over and suspend *all* other threads. When the
  // thread is done stepping over, it will call the process |OnBreakpointFinishedSteppingOver|
  // function.
  virtual void ExecuteStepOver(DebuggedThread* thread) = 0;

  // Frees all the suspension and exception resources held by the breakpoint. This is called by the
  // process.
  //
  // See the comments of |EndStepOver| for more details.
  virtual void StepOverCleanup(DebuggedThread* thread) = 0;

  virtual debug::Status Update() = 0;

 protected:
  DebuggedProcess* process_;  // Not-owning.
  uint64_t address_;

 private:
  virtual debug::Status Uninstall(DebuggedThread* thread) = 0;
  virtual debug::Status Uninstall() = 0;  // Uninstall for all the threads.

  // Breakpoints that refer to this ProcessBreakpoint. More than one Breakpoint can refer to the
  // same memory address.
  std::vector<Breakpoint*> breakpoints_;

  fxl::WeakPtrFactory<ProcessBreakpoint> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessBreakpoint);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_BREAKPOINT_H_
