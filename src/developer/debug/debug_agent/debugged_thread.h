// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_DEBUGGED_THREAD_H_
#define GARNET_BIN_DEBUG_AGENT_DEBUGGED_THREAD_H_

#include <lib/zx/thread.h>

#include "src/lib/fxl/macros.h"
#include "src/developer/debug/ipc/protocol.h"

struct zx_thread_state_general_regs;

namespace debug_agent {

class DebugAgent;
class DebuggedProcess;
class ProcessBreakpoint;

enum class ThreadCreationOption {
  // Already running, don't do anything
  kRunningKeepRunning,

  // Already suspended, keep it suspended
  kSuspendedKeepSuspended,

  // Already suspended, run it
  kSuspendedShouldRun
};

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
  const char* SuspendReasonToString(SuspendReason);

  // When a thread is first created and we get a notification about it, it
  // will be suspended, but when we attach to a process with existing threads
  // it won't in in this state. The |starting| flag indicates that this is
  // a thread discovered via a debug notification.
  DebuggedThread(DebuggedProcess* process, zx::thread thread,
                 zx_koid_t thread_koid, ThreadCreationOption option);
  virtual ~DebuggedThread();

  zx::thread& thread() { return thread_; }
  const zx::thread& thread() const { return thread_; }
  zx_koid_t koid() const { return koid_; }

  void OnException(uint32_t type);

  // Pauses execution of the thread. Returns true if the pause was successful.
  // Returns false on error or of the thread was already stopped. Pausing
  // happens asynchronously so the thread will not necessarily have stopped
  // when this returns.
  bool Pause();

  // Resumes execution of the thread. The thread should currently be in a
  // stopped state. If it's not stopped, this will be ignored.
  void Resume(const debug_ipc::ResumeRequest& request);

  // Fills the thread status record. If full_stack is set, a full backtrace
  // will be generated, otherwise a minimal one will be generated.
  //
  // If optional_regs is non-null, it should point to the current registers of
  // the thread. If null, these will be fetched automatically. See the global
  // FillThreadRecord() for more.
  void FillThreadRecord(debug_ipc::ThreadRecord::StackAmount stack_amount,
                        const zx_thread_state_general_regs* optional_regs,
                        debug_ipc::ThreadRecord* record) const;

  // Register reading and writing.
  void ReadRegisters(
      const std::vector<debug_ipc::RegisterCategory::Type>& cats_to_get,
      std::vector<debug_ipc::RegisterCategory>* out) const;
  zx_status_t WriteRegisters(const std::vector<debug_ipc::Register>& regs);

  // Sends a notification to the client about the state of this thread.
  void SendThreadNotification() const;

  // Notification that a ProcessBreakpoint is about to be deleted.
  void WillDeleteProcessBreakpoint(ProcessBreakpoint* bp);

 private:
  enum class OnStop {
    kIgnore,  // Don't do anything, keep the thread stopped and don't notify.
    kSendNotification  // Send client notification like normal.
  };

  // Handles a software breakpoint exception, updating the state as necessary.
  // If the address corresponds to a breakpoint we have set, it will call
  // UpdateForHitProcessBreakpoint (see below).
  OnStop UpdateForSoftwareBreakpoint(
      zx_thread_state_general_regs* regs,
      std::vector<debug_ipc::BreakpointStats>* hit_breakpoints);

  OnStop UpdateForHardwareBreakpoint(
      zx_thread_state_general_regs* regs,
      std::vector<debug_ipc::BreakpointStats>* hit_breakpoints);

  // Handles an exception corresponding to a ProcessBreakpoint. All
  // Breakpoints affected will have their updated stats added to
  // *hit_breakpoints.
  //
  // WARNING: The ProcessBreakpoint argument could be deleted in this call
  // if it was a one-shot breakpoint.
  void UpdateForHitProcessBreakpoint(
      debug_ipc::BreakpointType exception_type,
      ProcessBreakpoint* process_breakpoint, zx_thread_state_general_regs* regs,
      std::vector<debug_ipc::BreakpointStats>* hit_breakpoints);

  // Resumes the thread according to the current run mode.
  void ResumeForRunMode();

  // Sets or clears the single step bit on the thread.
  void SetSingleStep(bool single_step);

  DebugAgent* debug_agent_;   // Non-owning.
  DebuggedProcess* process_;  // Non-owning.
  zx::thread thread_;
  zx_koid_t koid_;

  // The main thing we're doing. When automatically resuming, this will be
  // what happens.
  debug_ipc::ResumeRequest::How run_mode_ =
      debug_ipc::ResumeRequest::How::kContinue;

  // When run_mode_ == kStepInRange, this defines the range (end non-inclusive).
  uint64_t step_in_range_begin_ = 0;
  uint64_t step_in_range_end_ = 0;

  // This is the reason for the thread suspend. This controls how the thread
  // will be resumed. SuspendReason::kOther implies the suspend_token_ is
  // valid.
  SuspendReason suspend_reason_ = SuspendReason::kNone;
  zx::suspend_token suspend_token_;

  // This can be set in two cases:
  // - When suspended after hitting a breakpoint, this will be the breakpoint
  //   that was hit.
  // - When single-stepping over a breakpoint, this will be the breakpoint
  //   being stepped over.
  ProcessBreakpoint* current_breakpoint_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedThread);
};

}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_DEBUGGED_THREAD_H_
