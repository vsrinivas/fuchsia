// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_PROCESS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_PROCESS_H_

#include <map>
#include <memory>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/module_list.h"
#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/debug_agent/process_handle_observer.h"
#include "src/developer/debug/debug_agent/stdio_handles.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/buffered_zx_socket.h"
#include "src/developer/debug/shared/message_loop.h"
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
  StdioHandles stdio;

  // Whether this process was obtained via a process limbo.
  // This is relevant when attempting to kill the process, as handles obtained via the limbo do not
  // have the ZX_RIGHT_DESTROY right. The way to "kill" them is to re send them to the limbo and
  // then release it from it.
  bool from_limbo = false;
};

class DebuggedProcess : public ProcessHandleObserver {
 public:
  using WatchpointMap =
      std::map<debug::AddressRange, std::unique_ptr<Watchpoint>, debug::AddressRangeBeginCmp>;

  // Caller must call Init after construction.
  explicit DebuggedProcess(DebugAgent*);
  virtual ~DebuggedProcess();

  zx_koid_t koid() const { return process_handle_->GetKoid(); }
  DebugAgent* debug_agent() const { return debug_agent_; }

  const ProcessHandle& process_handle() const { return *process_handle_; }
  ProcessHandle& process_handle() { return *process_handle_; }

  const ModuleList& module_list() const { return module_list_; }

  // The object is not usable if |Init| is not called or fails.
  debug::Status Init(DebuggedProcessCreateInfo create_info);

  void SetStdout(OwnedStdioHandle handle);
  void SetStderr(OwnedStdioHandle handle);

  // IPC handlers.
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
  void OnSaveMinidump(const debug_ipc::SaveMinidumpRequest& request,
                      debug_ipc::SaveMinidumpReply* reply);

  void InjectThreadForTest(std::unique_ptr<DebuggedThread> thread);

  // Synchronously pauses all threads in the process from the perspective of the client. This issues
  // ClientSuspend() on all threads (see that for more on what "client" means).
  //
  // The except_thread can be passed which indicates a thread to ship when suspending. This is for
  // certain operations that want to do something to all other threads.
  //
  // The affected threads are returned. If a thread is already in a client suspend, it will not be
  // affected and it will not be returned in the result.
  std::vector<debug_ipc::ProcessThreadId> ClientSuspendAllThreads(
      zx_koid_t except_thread = ZX_KOID_INVALID);

  // Returns the thread or null if there is no known thread for this koid.
  DebuggedThread* GetThread(zx_koid_t thread_koid) const;
  std::vector<DebuggedThread*> GetThreads() const;

  // Populates the thread map with the current threads for this process.
  // This function does not notify the client of thread start, but rather updates the internal
  // thread state according to the underlying zircon truth.
  void PopulateCurrentThreads();

  // Returns the information for all current threads. This gets minimal stacks.
  std::vector<debug_ipc::ThreadRecord> GetThreadRecords() const;

  // Checks if a breakpoint at the given address is the loader's internal one.
  // If it is, handles it and returns true. If it's not, does nothing and returns false.
  bool HandleLoaderBreakpoint(uint64_t address);

  // Suspend all thread and send the module list. This does not refresh the module list.
  // The client expects the threads to be suspended so it can resolve breakpoints and resume them.
  virtual void SuspendAndSendModules();

  // Looks for breakpoints at the given address. Null if no breakpoints are at that address.
  virtual SoftwareBreakpoint* FindSoftwareBreakpoint(uint64_t address) const;
  virtual HardwareBreakpoint* FindHardwareBreakpoint(uint64_t address) const;
  virtual Watchpoint* FindWatchpoint(const debug::AddressRange&) const;

  debug::Status RegisterBreakpoint(Breakpoint* bp, uint64_t address);
  void UnregisterBreakpoint(Breakpoint* bp, uint64_t address);

  debug::Status RegisterWatchpoint(Breakpoint* bp, const debug::AddressRange& range);
  void UnregisterWatchpoint(Breakpoint* bp, const debug::AddressRange& range);

  // Each time a thread attempts to step over a breakpoint, the breakpoint will enqueue itself and
  // the thread into the step over queue. The step over queue is used so that there is only one
  // breakpoint being stepped over at a time.
  //
  // Enqueuing the breakpoint does not mean that the step over begins immediately, but rather the
  // process will call the |ExecuteStepOver| method on the breakpoint once its turn has come up in
  // the queue.
  //
  // In some error cases this might happen twice for the same thread. For example, if there is an
  // error clearing the breakpoint instruction, attempting to clear it and then single-step it will
  // just hit the same breakpoint again and the "step over" will never complete.
  //
  // If this happens the original "step over" request will be silently dropped. Otherwise, the
  // step queue will be recursively waiting for itself and can never continue. All other breakpoints
  // will still be waiting behind this failed step, but at least it could theoretically continue if
  // the breakpoint clearing works in a future try.
  void EnqueueStepOver(ProcessBreakpoint* process_breakpoint, DebuggedThread* thread);

  // Called by the currently stepping over breakpoint when it's done. It will execute the next
  // enqueued breakpoint. If there are no more breakpoints enqueued, this will let all the
  // breakpoints know so that it can resume the stepped over threads.
  void OnBreakpointFinishedSteppingOver();

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

  // Queue of breakpoints that are currently being stepped over.
  //
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

  // Sends a IO notification over to the client.
  void SendIO(debug_ipc::NotifyIO::Type, const std::vector<char>& data);

  // Deletes any elements in the step over queue that are no longer valid. This happens when either
  // the thread or the breakpoint went away while the ticket was waiting within the queue.
  //
  // If the |thread| parameter is non-null, ALL requests from that thread will be deleted in
  // addition to the normal pruning behavior.
  void PruneStepOverQueue(DebuggedThread* optional_thread);

  debug::Status RegisterSoftwareBreakpoint(Breakpoint* bp, uint64_t address);
  void UnregisterSoftwareBreakpoint(Breakpoint* bp, uint64_t address);
  debug::Status RegisterHardwareBreakpoint(Breakpoint* bp, uint64_t address);
  void UnregisterHardwareBreakpoint(Breakpoint* bp, uint64_t address);

  DebugAgent* debug_agent_ = nullptr;  // Non-owning.

  std::unique_ptr<ProcessHandle> process_handle_;

  // Current modules loaded in the process.
  ModuleList module_list_;

  std::map<zx_koid_t, std::unique_ptr<DebuggedThread>> threads_;

  // Indicates that new threads, if discovered, should be suspended.
  // New thread could be spawned even when all known threads are suspended.
  bool suspend_new_threads_ = false;

  // Maps addresses to the ProcessBreakpoint at a location.
  std::map<uint64_t, std::unique_ptr<SoftwareBreakpoint>> software_breakpoints_;
  std::map<uint64_t, std::unique_ptr<HardwareBreakpoint>> hardware_breakpoints_;
  WatchpointMap watchpoints_;

  std::deque<StepOverTicket> step_over_queue_;

  // Non-null only if the corresponding stream is hooked up.
  std::unique_ptr<BufferedStdioHandle> stdout_;
  std::unique_ptr<BufferedStdioHandle> stderr_;

  // Whether this process was obtained from limbo or not. The agent will check this information
  // when it tries to kill this process in order to determine whether the ZX_ERR_BAD_ACCESS is
  // expected (limbo handles do not have ZX_RIGHT_DESTROY right) or it is an actual error.
  bool from_limbo_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedProcess);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_PROCESS_H_
