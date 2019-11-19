// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_PROCESS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_PROCESS_H_

#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <lib/zx/thread.h>

#include <map>
#include <memory>
#include <vector>

#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/process_memory_accessor.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/buffered_zx_socket.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/zircon_exception_watcher.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/macros.h"

namespace debug_agent {

class Breakpoint;
class DebugAgent;
class HardwareBreakpoint;
class ObjectProvider;
class ProcessBreakpoint;
class ProcessWatchpoint;
class SoftwareBreakpoint;
class Watchpoint;

struct DebuggedProcessCreateInfo {
  DebuggedProcessCreateInfo();

  // Constructor with only the required fields.
  DebuggedProcessCreateInfo(zx_koid_t koid, std::string name, zx::process);
  DebuggedProcessCreateInfo(zx_koid_t koid, std::string name, zx::process,
                            std::shared_ptr<arch::ArchProvider>, std::shared_ptr<ObjectProvider>);

  // Required.
  zx_koid_t koid = 0;
  zx::process handle;

  // Required.
  std::shared_ptr<arch::ArchProvider> arch_provider;
  std::shared_ptr<ObjectProvider> object_provider;

  // Optional.
  // This is meant as a way to override the memory accessor that a process uses, mostly for testing.
  // For the default case, do not set this and the process will create a memory accessor for itself.
  std::unique_ptr<ProcessMemoryAccessor> memory_accessor;

  // Optional.
  std::string name;
  zx::socket out;  // stdout.
  zx::socket err;  // stderr.
};

// Creates a CreateInfo struct from only the required fields.

class DebuggedProcess : public debug_ipc::ZirconExceptionWatcher {
 public:
  using WatchpointMap = std::map<debug_ipc::AddressRange, std::unique_ptr<Watchpoint>,
                                 debug_ipc::AddressRangeBeginCmp>;

  // Caller must call Init immediately after construction and delete the
  // object if that fails.
  DebuggedProcess(DebugAgent*, DebuggedProcessCreateInfo&&);
  virtual ~DebuggedProcess();

  zx_koid_t koid() const { return koid_; }
  DebugAgent* debug_agent() const { return debug_agent_; }
  zx::process& handle() { return handle_; }
  uint64_t dl_debug_addr() const { return dl_debug_addr_; }

  const std::string& name() const { return name_; }

  // Returns true on success. On failure, the object may not be used further.
  // |object_provider| gives a view of the Zircon process tree. Can be overriden for test purposes.
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

  // Pauses all threads in the process. If non-null, the paused_koids vector
  // will be populated with the koids of all threads paused by this operation.
  //
  // If |synchronous| is false, this call will send the suspend commands to the
  // kernel and return immediately. It will block on all the suspend signals
  // otherwise.
  void SuspendAll(bool synchronous = false, std::vector<zx_koid_t>* suspended_koids = nullptr);

  // Returns the thread or null if there is no known thread for this koid.
  virtual DebuggedThread* GetThread(zx_koid_t thread_koid) const;
  virtual std::vector<DebuggedThread*> GetThreads() const;

  // Populates the thread map with the current threads for this process.
  // This function does not notify the client of thread start, but rather updates the internal
  // thread state according to the underlying zircon truth.
  void PopulateCurrentThreads();

  // Appends the information for all current threads. This writes minimal
  // stacks.
  void FillThreadRecords(std::vector<debug_ipc::ThreadRecord>* threads);

  // Attempts to load the debug_state_ value from the
  // ZX_PROP_PROCESS_DEBUG_ADDR of the debugged process. Returns true if it
  // is now set. False means it remains unset. Normally the first time this
  // returns true would need to be followed up with a SendModuleNotification.
  bool RegisterDebugState();

  // If the process can know its modules, suspend all thread and send the module list.
  //
  // This is used in the case where we attach to an existing process or a new forked process and the
  // debug address is known. The client expects the threads to be suspended so it can resolve
  // breakpoints and resume them.
  virtual void SuspendAndSendModulesIfKnown();

  // Sends the currently loaded modules to the client with the given list of paused threads.
  void SendModuleNotification(std::vector<uint64_t> paused_thread_koids);

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

 protected:
  std::shared_ptr<arch::ArchProvider> arch_provider_;
  std::shared_ptr<ObjectProvider> object_provider_;

  std::unique_ptr<ProcessMemoryAccessor> memory_accessor_;

 private:
  // ZirconExceptionWatcher implementation.
  void OnThreadStarting(zx::exception exception_token, zx_exception_info_t exception_info) override;
  void OnProcessTerminated(zx_koid_t process_koid) override;
  void OnThreadExiting(zx::exception exception_token, zx_exception_info_t exception_info) override;
  void OnException(zx::exception exception_token, zx_exception_info_t exception_info) override;

  void OnStdout(bool close);
  void OnStderr(bool close);

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

  zx_status_t RegisterSoftwareBreakpoint(Breakpoint* bp, uint64_t address);
  void UnregisterSoftwareBreakpoint(Breakpoint* bp, uint64_t address);
  zx_status_t RegisterHardwareBreakpoint(Breakpoint* bp, uint64_t address);
  void UnregisterHardwareBreakpoint(Breakpoint* bp, uint64_t address);

  DebugAgent* debug_agent_ = nullptr;  // Non-owning.

  zx_koid_t koid_;
  zx::process handle_;

  std::string name_;

  // Address in the debugged program of the dl_debug_state in ld.so.
  uint64_t dl_debug_addr_ = 0;

  // Handle for watching the process exceptions.
  debug_ipc::MessageLoop::WatchHandle process_watch_handle_;

  std::map<zx_koid_t, std::unique_ptr<DebuggedThread>> threads_;

  // Maps addresses to the ProcessBreakpoint at a location.
  std::map<uint64_t, std::unique_ptr<SoftwareBreakpoint>> software_breakpoints_;
  std::map<uint64_t, std::unique_ptr<HardwareBreakpoint>> hardware_breakpoints_;
  WatchpointMap watchpoints_;

  std::deque<StepOverTicket> step_over_queue_;

  debug_ipc::BufferedZxSocket stdout_;
  debug_ipc::BufferedZxSocket stderr_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedProcess);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUGGED_PROCESS_H_
