// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_PROCESS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_PROCESS_H_

#include <map>
#include <memory>
#include <vector>

#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/module_list.h"
#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/debug_agent/process_handle_observer.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/buffered_zx_socket.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/macros.h"

namespace debug_agent {

class Breakpoint;
class DebugAgent;
class HardwareBreakpoint;
class ProcessBreakpoint;
class ProcessWatchpoint;
class SoftwareBreakpoint;
class Watchpoint;

struct DebuggedProcessCreateInfo {
  explicit DebuggedProcessCreateInfo(std::unique_ptr<ProcessHandle> handle);

  // Required.
  std::unique_ptr<ProcessHandle> handle;

  // Optional.
  zx::socket out;  // stdout.
  zx::socket err;  // stderr.

  // Whether this process was obtained via a process limbo.
  // This is relevant when attempting to kill the process, as handles obtained via the limbo do not
  // have the ZX_RIGHT_DESTROY right. The way to "kill" them is to re send them to the limbo and
  // then release it from it.
  bool from_limbo = false;
};

class DebuggedProcess : public ProcessHandleObserver {
 public:
  using WatchpointMap = std::map<debug_ipc::AddressRange, std::unique_ptr<Watchpoint>,
                                 debug_ipc::AddressRangeBeginCmp>;

  // Caller must call Init immediately after construction and delete the
  // object if that fails.
  DebuggedProcess(DebugAgent*, DebuggedProcessCreateInfo&&);
  virtual ~DebuggedProcess();

  zx_koid_t koid() const { return process_handle_->GetKoid(); }
  DebugAgent* debug_agent() const { return debug_agent_; }

  const ProcessHandle& process_handle() const { return *process_handle_; }
  ProcessHandle& process_handle() { return *process_handle_; }

  // TODO(brettw) remove this and have all callers use thread_handle().
  zx::process& handle() { return process_handle_->GetNativeHandle(); }
  const zx::process& handle() const { return process_handle_->GetNativeHandle(); }

  const ModuleList& module_list() const { return module_list_; }

  // Returns true on success. On failure, the object may not be used further.
  zx_status_t Init();

  // IPC handlers.
  void OnPause(const debug_ipc::PauseRequest& request, debug_ipc::PauseReply* reply);
  void OnResume(const debug_ipc::ResumeRequest& request);
  void OnReadMemory(const debug_ipc::ReadMemoryRequest& request, debug_ipc::ReadMemoryReply* reply);
  void OnKill(const debug_ipc::KillRequest& request, debug_ipc::KillReply* reply);
  void OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                      debug_ipc::AddressSpaceReply* reply);
  void OnModules(debug_ipc::ModulesReply* reply);
  void OnWriteMemory(const debug_ipc::WriteMemoryRequest& request,
                     debug_ipc::WriteMemoryReply* reply);
  void OnLoadInfoHandleTable(const debug_ipc::LoadInfoHandleTableRequest& request,
                             debug_ipc::LoadInfoHandleTableReply* reply);

  void InjectThreadForTest(std::unique_ptr<DebuggedThread> thread);

  // Synchronously pauses all threads in the process from the perspective of the client. This issues
  // ClientSuspend() on all threads (see that for more on what "client" means).
  void ClientSuspendAllThreads();

  // Returns the thread or null if there is no known thread for this koid.
  DebuggedThread* GetThread(zx_koid_t thread_koid) const;
  std::vector<DebuggedThread*> GetThreads() const;

  // Populates the thread map with the current threads for this process.
  // This function does not notify the client of thread start, but rather updates the internal
  // thread state according to the underlying zircon truth.
  void PopulateCurrentThreads();

  // Returns the information for all current threads. This gets minimal stacks.
  std::vector<debug_ipc::ThreadRecord> GetThreadRecords() const;

  // Checks if this breakpoint is a special internal one. If it is, handles it and returns true.
  // If it's not special, does nothing and returns false. The parameter indicates the breakpoint
  // that was hit. If the breakpoint was a hardcoded one, the parameter should be null.
  //
  // This will check for the different types of loader breakpoints.
  enum class SpecialBreakpointResult { kNotSpecial, kContinue, kKeepSuspended };
  SpecialBreakpointResult HandleSpecialBreakpoint(ProcessBreakpoint* optional_bp);

  // If the process can know its modules, suspend all thread and send the module list. This does not
  // refresh the module list.
  //
  // This is used in the case where we attach to an existing process or a new forked process and the
  // debug address is known. The client expects the threads to be suspended so it can resolve
  // breakpoints and resume them.
  virtual void SuspendAndSendModulesIfKnown();

  // Looks for breakpoints at the given address. Null if no breakpoints are at that address.
  virtual SoftwareBreakpoint* FindSoftwareBreakpoint(uint64_t address) const;
  virtual HardwareBreakpoint* FindHardwareBreakpoint(uint64_t address) const;
  virtual Watchpoint* FindWatchpoint(const debug_ipc::AddressRange&) const;

  zx_status_t RegisterBreakpoint(Breakpoint* bp, uint64_t address);
  void UnregisterBreakpoint(Breakpoint* bp, uint64_t address);

  zx_status_t RegisterWatchpoint(Breakpoint* bp, const debug_ipc::AddressRange& range);
  void UnregisterWatchpoint(Breakpoint* bp, const debug_ipc::AddressRange& range);

  // Each time a thread attempts to step over a breakpoint, the breakpoint will enqueue itself and
  // the thread into the step over queue. The step over queue is used so that there is only one
  // breakpoint being stepped over at a time.
  //
  // Enqueuing the breakpoint does not mean that the step over begins immediately, but rather the
  // process will call the |ExecuteStepOver| method on the breakpoint once its turn has come up in
  // the queue.
  void EnqueueStepOver(ProcessBreakpoint* process_breakpoint, DebuggedThread* thread);

  // Called by the currently stepping over breakpoint when it's done. It will execute the next
  // enqueued breakpoint. If there are no more breakpoints enqueued, this will let all the
  // breakpoints know so that it can resume the stepped over threads.
  void OnBreakpointFinishedSteppingOver();

  // Queue of breakpoints that are currently being stepped over.
  // As stepping over requires suspending all the threads, doing multiple at a time has a fair
  // chance of introducing deadlocks. We use this queue to serialize the stepping over, so only
  // one process breakpoint is being stepped over at a time.
  struct StepOverTicket {
    fxl::WeakPtr<ProcessBreakpoint> process_breakpoint;
    fxl::WeakPtr<DebuggedThread> thread;

    bool is_valid() const { return !!process_breakpoint && !!thread; }
  };
  const std::deque<StepOverTicket>& step_over_queue() const { return step_over_queue_; }

  const std::map<uint64_t, std::unique_ptr<SoftwareBreakpoint>>& software_breakpoints() const {
    return software_breakpoints_;
  }

  const std::map<uint64_t, std::unique_ptr<HardwareBreakpoint>>& hardware_breakpoints() const {
    return hardware_breakpoints_;
  }

  const WatchpointMap& watchpoints() const { return watchpoints_; }

  bool from_limbo() const { return from_limbo_; }

 private:
  // ProcessHandleObserver implementation.
  void OnProcessTerminated() override;
  void OnThreadStarting(std::unique_ptr<ExceptionHandle> exception) override;
  void OnThreadExiting(std::unique_ptr<ExceptionHandle> exception) override;
  void OnException(std::unique_ptr<ExceptionHandle> exception) override;

  void OnStdout(bool close);
  void OnStderr(bool close);

  // Sends the currently loaded modules to the client with the current list of threads. This does
  // not refresh the module cache. All threads are assumed to be paused before this call.
  void SendModuleNotification();

  // Sends a IO notification over to the client.
  void SendIO(debug_ipc::NotifyIO::Type, const std::vector<char>& data);

  // This function will gracefully detach from the underlying zircon process.
  // Detaching correctly requires several steps:
  //
  // 1. Remove the installed breakpoints.
  //
  // 2. Resume threads from the exception. Only threads that are stopped on an
  // exception should be resumed. This is because otherwise zircon will treat
  // this exception as unhandled and will bubble up the exception upwards,
  // probably resulting in a crash.
  //
  // 3. Unbind the exception port.
  void DetachFromProcess();

  // Deletes any elements in the step over queue that are no longer valid.
  // This happens when either the thread or the breakpoint went away while the ticket was waiting
  // within the queue.
  void PruneStepOverQueue();

  // Attempts to load the debug_state_ value from the
  // ZX_PROP_PROCESS_DEBUG_ADDR of the debugged process. Returns true if it
  // is now set. False means it remains unset. Normally the first time this
  // returns true would need to be followed up with a SendModuleNotification.
  bool RegisterDebugState();

  zx_status_t RegisterSoftwareBreakpoint(Breakpoint* bp, uint64_t address);
  void UnregisterSoftwareBreakpoint(Breakpoint* bp, uint64_t address);
  zx_status_t RegisterHardwareBreakpoint(Breakpoint* bp, uint64_t address);
  void UnregisterHardwareBreakpoint(Breakpoint* bp, uint64_t address);

  DebugAgent* debug_agent_ = nullptr;  // Non-owning.

  std::unique_ptr<ProcessHandle> process_handle_;

  // Address in the debugged program of the dl_debug_state in ld.so.
  uint64_t dl_debug_addr_ = 0;

  // Current modules loaded in the process.
  ModuleList module_list_;

  // Breakpoint used to catch shared library loads. This will be set when the dl_debug_addr_ is
  // known.
  std::unique_ptr<Breakpoint> loader_breakpoint_;

  std::map<zx_koid_t, std::unique_ptr<DebuggedThread>> threads_;

  // Maps addresses to the ProcessBreakpoint at a location.
  std::map<uint64_t, std::unique_ptr<SoftwareBreakpoint>> software_breakpoints_;
  std::map<uint64_t, std::unique_ptr<HardwareBreakpoint>> hardware_breakpoints_;
  WatchpointMap watchpoints_;

  std::deque<StepOverTicket> step_over_queue_;

  debug_ipc::BufferedZxSocket stdout_;
  debug_ipc::BufferedZxSocket stderr_;

  // Whether this process was obtained from limbo or not. The agent will check this information
  // when it tries to kill this process in order to determine whether the ZX_ERR_BAD_ACCESS is
  // expected (limbo handles do not have ZX_RIGHT_DESTROY right) or it is an actual error.
  bool from_limbo_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedProcess);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_PROCESS_H_
