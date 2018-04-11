// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/thread.h>

#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/public/lib/fxl/macros.h"

struct zx_thread_state_general_regs;

namespace debug_agent {

class DebugAgent;
class DebuggedProcess;
class ProcessBreakpoint;

class DebuggedThread {
 public:
  // The SuspendReason indicates why the thread was suspended from our
  // perspective. This doesn't take into account other things on the system
  // that may have suspended a thread. If somebody does this, the thread will
  // be suspended but our state will be kNone (meaning resuming it is not
  // something we can do).
  enum class SuspendReason {
    // Not suspended.
    kNone,

    // Exception from the program.
    kException,

    // Anything else.
    kOther
  };

  // When a thread is first created and we get a notification about it, it
  // will be suspended, but when we attach to a process with existing threads
  // it won't in in this state. The |starting| flag indicates that this is
  // a thread discoverd via a debug notification.
  DebuggedThread(DebuggedProcess* process,
                 zx::thread thread,
                 zx_koid_t thread_koid,
                 bool starting);
  ~DebuggedThread();

  zx::thread& thread() { return thread_; }
  zx_koid_t koid() const { return koid_; }

  void OnException(uint32_t type);

  // Pauses execution of the thread. If it is already stopped, this will be
  // ignored. Pausing happens asynchronously so the thread will not necessarily
  // have stopped when this returns.
  void Pause();

  // Resumes execution of the thread. The thead should currently be in a
  // stopped state. If it's not stopped, this will be ignored.
  void Resume(debug_ipc::ResumeRequest::How how);

  // Fills in the backtrace if available. Otherwise fills in nothing.
  void GetBacktrace(std::vector<debug_ipc::StackFrame>* frames) const;

  // Sends a notification to the client about the state of this thread.
  void SendThreadNotification() const;

 private:
  enum class AfterBreakpointStep {
    kContinue,  // Resume execution.
    kBreak      // Send the client the exception and keep stopped.
  };

  void UpdateForSoftwareBreakpoint(zx_thread_state_general_regs* regs);

  // Sets or clears the single step bit on the thread.
  void SetSingleStep(bool single_step);

  DebugAgent* debug_agent_;   // Non-owning.
  DebuggedProcess* process_;  // Non-owning.
  zx::thread thread_;
  zx_koid_t koid_;

  // This is the reason for the thread suspend. This controls how the thread
  // will be resumed. Note that when a breakpoint is hit and other threads
  // are suspended, only the thread that hit the breakpoint will have
  // suspend_reason_ == kBreakpoint. The other threads will be manually
  // suspended which will be kOther.
  SuspendReason suspend_reason_ = SuspendReason::kNone;

  // This can be set in two cases:
  // - When suspended after hitting a breakpoint, this will be the breakpoint
  //   that was hit.
  // - When single-stepping over a breakpoint, this will be the breakpoint
  //   being stepped over.
  ProcessBreakpoint* current_breakpoint_ = nullptr;

  AfterBreakpointStep after_breakpoint_step_ = AfterBreakpointStep::kContinue;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedThread);
};

}  // namespace debug_agent
