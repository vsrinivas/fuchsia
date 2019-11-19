// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_THREAD_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_THREAD_H_

#include <lib/zx/exception.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/object_provider.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"

struct zx_thread_state_general_regs;

namespace debug_agent {

class DebugAgent;
class DebuggedProcess;
class ObjectProvider;
class ProcessBreakpoint;
class Watchpoint;

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
  // Represents a ref-counted suspend token to the debugged thread.
  // As long as one of these token is valid, the thread will maintain suspended.
  class SuspendToken {
   public:
    ~SuspendToken();

   private:
    SuspendToken(DebuggedThread*);

    fxl::WeakPtr<DebuggedThread> thread_;

    FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(SuspendToken);
    friend class ::debug_agent::DebuggedThread;
  };

  // Represents the state the client thinks this thread is in. Certain
  // operations can suspend all the threads of a process and the debugger needs
  // to know which threads should remain suspended after that operation is done.
  enum class ClientState {
    kRunning,
    kPaused,
  };
  const char* ClientStateToString(ClientState);

  struct CreateInfo {
    DebuggedProcess* process = nullptr;
    zx_koid_t koid = 0;
    zx::thread handle;
    ThreadCreationOption creation_option = ThreadCreationOption::kRunningKeepRunning;

    zx::exception exception;  // Optional.

    std::shared_ptr<arch::ArchProvider> arch_provider;
    std::shared_ptr<ObjectProvider> object_provider;
  };
  DebuggedThread(DebugAgent*, CreateInfo&&);
  virtual ~DebuggedThread();

  const DebuggedProcess* process() const { return process_; }

  zx_koid_t koid() const { return koid_; }

  zx::thread& handle() { return handle_; }
  const zx::thread& handle() const { return handle_; }

  zx::exception& exception_handle() { return exception_handle_; }
  const zx::exception& exception_handle() const { return exception_handle_; }
  void set_exception_handle(zx::exception exception) { exception_handle_ = std::move(exception); }

  fxl::WeakPtr<DebuggedThread> GetWeakPtr();

  void OnException(zx::exception exception_handle, zx_exception_info_t exception_info);

  // Resumes execution of the thread. The thread should currently be in a
  // stopped state. If it's not stopped, this will be ignored.
  void Resume(const debug_ipc::ResumeRequest& request);

  // Resumes the thread according to the current run mode.
  void ResumeForRunMode();

  // Resume the thread from an exception.
  // If |exception_handle_| is not valid, this will no-op.
  virtual void ResumeException();

  // Resume the thread from a suspension.
  // if |suspend_token_| is not valid, this will no-op.
  virtual void ResumeSuspension();

  // Pauses execution of the thread. Pausing happens asynchronously so the
  // thread will not necessarily have stopped when this returns. Set the
  // |synchronous| flag for blocking on the suspended signal and make this call
  // block until the thread is suspended.
  //
  // |new_state| represents what the new state of the client should be. If no
  // change is wanted, you can use the overload that doesn't receives that.
  //
  // Returns true if the thread was running at the moment of this call being
  // made. Returns false if it was on a suspension condition (suspended or on an
  // exception).
  virtual bool Suspend(bool synchronous = false);

  // Pauses execution of the thread. Pausing happens asynchronously so the thread will not
  // necessarily have stopped when this returns. Set the |synchronous| flag for blocking on the
  // suspended signal and make this call block.
  //
  // Suspension is ref-counted on the thread. This is done by returning a suspend token that will
  // keep track of how many suspensions this thread has. As long as there is a valid one, the
  // thread will remain suspended.
  [[nodiscard]] virtual std::unique_ptr<SuspendToken> RefCountedSuspend(bool synchronous = false);

  // The typical suspend deadline users should use when suspending.
  static zx::time DefaultSuspendDeadline();

  // Waits on a suspension token.
  // Returns true if we could find a valid suspension condition (either
  // suspended or on an exception). False if timeout or error.
  virtual bool WaitForSuspension(zx::time deadline = DefaultSuspendDeadline());

  // Fills the thread status record. If full_stack is set, a full backtrace
  // will be generated, otherwise a minimal one will be generated.
  //
  // If optional_regs is non-null, it should point to the current registers of
  // the thread. If null, these will be fetched automatically (this is an
  // optimization for cases where the caller has already requested registers).
  virtual void FillThreadRecord(debug_ipc::ThreadRecord::StackAmount stack_amount,
                                const zx_thread_state_general_regs* optional_regs,
                                debug_ipc::ThreadRecord* record) const;

  // Register reading and writing. The "write" command also has an output parameter which includes a
  // validated version of all registers re-read (and possibly others if they're known) from the
  // kernel after the write.
  void ReadRegisters(const std::vector<debug_ipc::RegisterCategory>& cats_to_get,
                     std::vector<debug_ipc::Register>* out) const;
  zx_status_t WriteRegisters(const std::vector<debug_ipc::Register>& regs,
                             std::vector<debug_ipc::Register>* written);

  // Sends a notification to the client about the state of this thread.
  void SendThreadNotification() const;

  // Notification that a ProcessBreakpoint is about to be deleted.
  void WillDeleteProcessBreakpoint(ProcessBreakpoint* bp);

  ClientState client_state() const { return client_state_; }
  void set_client_state(ClientState cs) { client_state_ = cs; }

  bool running() const { return !IsSuspended() && !IsInException(); }

  virtual bool IsSuspended() const { return ref_counted_suspend_token_.is_valid(); }
  virtual bool IsInException() const { return exception_handle_.is_valid(); }

  int ref_counted_suspend_count() const { return suspend_count_; }

  bool stepping_over_breakpoint() const { return stepping_over_breakpoint_; }
  void set_stepping_over_breakpoint(bool so) { stepping_over_breakpoint_ = so; }

 protected:
  virtual void IncreaseSuspend();
  virtual void DecreaseSuspend();

  zx_koid_t koid_;
  zx::thread handle_;

 private:
  enum class OnStop {
    kIgnore,  // Don't do anything, keep the thread stopped and don't notify.
    kNotify,  // Send client notification like normal.
    kResume,  // The thread should be resumed from this exception.
  };

  void HandleSingleStep(debug_ipc::NotifyException*, zx_thread_state_general_regs*);
  void HandleGeneralException(debug_ipc::NotifyException*, zx_thread_state_general_regs*);
  void HandleSoftwareBreakpoint(debug_ipc::NotifyException*, zx_thread_state_general_regs*);
  void HandleHardwareBreakpoint(debug_ipc::NotifyException*, zx_thread_state_general_regs*);
  void HandleWatchpoint(debug_ipc::NotifyException*, zx_thread_state_general_regs*);

  void SendExceptionNotification(debug_ipc::NotifyException*, zx_thread_state_general_regs*);

  OnStop UpdateForSoftwareBreakpoint(zx_thread_state_general_regs* regs,
                                     std::vector<debug_ipc::BreakpointStats>* hit_breakpoints);

  // When hitting a SW breakpoint, the PC needs to be correctly re-set depending
  // on where the CPU leaves the PC after a SW exception.
  void FixSoftwareBreakpointAddress(ProcessBreakpoint* process_breakpoint,
                                    zx_thread_state_general_regs* regs);

  // Handles an exception corresponding to a ProcessBreakpoint. All
  // Breakpoints affected will have their updated stats added to
  // *hit_breakpoints.
  //
  // WARNING: The ProcessBreakpoint argument could be deleted in this call
  // if it was a one-shot breakpoint.
  void UpdateForHitProcessBreakpoint(debug_ipc::BreakpointType exception_type,
                                     ProcessBreakpoint* process_breakpoint,
                                     zx_thread_state_general_regs* regs,
                                     std::vector<debug_ipc::BreakpointStats>* hit_breakpoints);
  void UpdateForHitWatchpoint(Watchpoint* watchpoint,
                              std::vector<debug_ipc::BreakpointStats>* hit_breakpoints);

  // Sets or clears the single step bit on the thread.
  void SetSingleStep(bool single_step);

  DebugAgent* debug_agent_;   // Non-owning.
  DebuggedProcess* process_;  // Non-owning.

  // The main thing we're doing. When automatically resuming, this will be
  // what happens.
  debug_ipc::ResumeRequest::How run_mode_ = debug_ipc::ResumeRequest::How::kContinue;

  // When run_mode_ == kStepInRange, this defines the range (end non-inclusive).
  uint64_t step_in_range_begin_ = 0;
  uint64_t step_in_range_end_ = 0;

  // This is the state the client is considering this thread to be. This is used
  // for internal suspension the agent can do.
  ClientState client_state_ = ClientState::kRunning;

  int suspend_count_ = 0;
  // This permits users to simply call Suspend/Resume without having to worry about having to
  // track a suspend token. They could if they so wanted.
  std::unique_ptr<SuspendToken> local_suspend_token_;
  zx::suspend_token ref_counted_suspend_token_;

  // Active if the thread is currently on an exception.
  zx::exception exception_handle_;

  // Whether this thread is currently stepping over.
  bool stepping_over_breakpoint_ = false;

  // This can be set in two cases:
  // - When suspended after hitting a breakpoint, this will be the breakpoint
  //   that was hit.
  // - When single-stepping over a breakpoint, this will be the breakpoint
  //   being stepped over.
  ProcessBreakpoint* current_breakpoint_ = nullptr;

  std::shared_ptr<arch::ArchProvider> arch_provider_ = nullptr;
  std::shared_ptr<ObjectProvider> object_provider_ = nullptr;

  fxl::WeakPtrFactory<DebuggedThread> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedThread);

  friend class ::debug_agent::DebuggedThread::SuspendToken;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_THREAD_H_
