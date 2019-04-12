// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_DEBUGGED_PROCESS_H_
#define GARNET_BIN_DEBUG_AGENT_DEBUGGED_PROCESS_H_

#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <lib/zx/thread.h>

#include <map>
#include <memory>
#include <vector>

#include "src/developer/debug/debug_agent/process_memory_accessor.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/ipc/records_utils.h"
#include "src/developer/debug/shared/buffered_zx_socket.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/zircon_exception_watcher.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/macros.h"

namespace debug_agent {

class Breakpoint;
class DebugAgent;
class DebuggedThread;
class ProcessBreakpoint;
class ProcessWatchpoint;
class Watchpoint;

struct DebuggedProcessCreateInfo {
  DebuggedProcessCreateInfo();
  // Constructor with only the required fields.
  DebuggedProcessCreateInfo(zx_koid_t process_koid, zx::process);

  // Required.
  zx_koid_t koid = 0;
  zx::process handle;

  // Optional.
  std::string name;
  zx::socket out;  // stdout.
  zx::socket err;  // stderr.
};

// Creates a CreateInfo struct from only the required fields.

class DebuggedProcess : public debug_ipc::ZirconExceptionWatcher,
                        public ProcessMemoryAccessor {
 public:
  // Caller must call Init immediately after construction and delete the
  // object if that fails.
  DebuggedProcess(DebugAgent*, DebuggedProcessCreateInfo&&);
  virtual ~DebuggedProcess();

  zx_koid_t koid() const { return koid_; }
  DebugAgent* debug_agent() const { return debug_agent_; }
  zx::process& process() { return process_; }
  uint64_t dl_debug_addr() const { return dl_debug_addr_; }

  const std::string& name() const { return name_; }

  // Returns true on success. On failure, the object may not be used further.
  zx_status_t Init();

  // IPC handlers.
  void OnPause(const debug_ipc::PauseRequest& request);
  void OnResume(const debug_ipc::ResumeRequest& request);
  void OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                    debug_ipc::ReadMemoryReply* reply);
  void OnKill(const debug_ipc::KillRequest& request,
              debug_ipc::KillReply* reply);
  void OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                      debug_ipc::AddressSpaceReply* reply);
  void OnModules(debug_ipc::ModulesReply* reply);
  void OnWriteMemory(const debug_ipc::WriteMemoryRequest& request,
                     debug_ipc::WriteMemoryReply* reply);

  // Pauses all threads in the process. If non-null, the paused_koids vector
  // will be populated with the koids of all threads paused by this operation.
  void SuspendAll(std::vector<uint64_t>* suspended_koids = nullptr);

  // Returns the thread or null if there is no known thread for this koid.
  virtual DebuggedThread* GetThread(zx_koid_t thread_koid) const;
  virtual std::vector<DebuggedThread*> GetThreads() const;

  // Populates the thread map with the current threads for this process, and
  // sends the list to the client. Used after an attach where we will not get
  // new thread notifications.
  void PopulateCurrentThreads();

  // Attempts to load the debug_state_ value from the
  // ZX_PROP_PROCESS_DEBUG_ADDR of the debugged process. Returns true if it
  // is now set. False means it remains unset. Normally the first time this
  // returns true would need to be followed up with a SendModuleNotification.
  bool RegisterDebugState();

  // Sends the currently loaded modules to the client with the given list
  // of paused threads.
  void SendModuleNotification(std::vector<uint64_t> paused_thread_koids);

  // Looks for breakpoints at the given address. Null if no breakpoints are
  // at that address.
  ProcessBreakpoint* FindProcessBreakpointForAddr(uint64_t address);

  // Notifications when breakpoints are added or removed that affect this
  // process.
  zx_status_t RegisterBreakpoint(Breakpoint* bp, uint64_t address);
  void UnregisterBreakpoint(Breakpoint* bp, uint64_t address);

  zx_status_t RegisterWatchpoint(Watchpoint*, const debug_ipc::AddressRange&);
  void UnregisterWatchpoint(Watchpoint*, const debug_ipc::AddressRange&);

 private:
  // ZirconExceptionWatcher implementation.
  void OnProcessTerminated(zx_koid_t process_koid) override;
  void OnThreadStarting(zx_koid_t process_koid, zx_koid_t thread_koid) override;
  void OnThreadExiting(zx_koid_t process_koid, zx_koid_t thread_koid) override;
  void OnException(zx_koid_t process_koid, zx_koid_t thread_koid,
                   uint32_t type) override;

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

  // ProcessMemoryAccessor implementation.
  zx_status_t ReadProcessMemory(uintptr_t address, void* buffer, size_t len,
                                size_t* actual) override;
  zx_status_t WriteProcessMemory(uintptr_t address, const void* buffer,
                                 size_t len, size_t* actual) override;

  DebugAgent* debug_agent_;  // Non-owning.

  zx_koid_t koid_;
  zx::process process_;
  std::string name_;

  // Address in the debugged program of the dl_debug_state in ld.so.
  uint64_t dl_debug_addr_ = 0;

  // Handle for watching the process exceptions.
  debug_ipc::MessageLoop::WatchHandle process_watch_handle_;

  std::map<zx_koid_t, std::unique_ptr<DebuggedThread>> threads_;

  // Maps addresses to the ProcessBreakpoint at a location. The
  // ProcessBreakpoint can hold multiple Breakpoint objects.
  std::map<uint64_t, std::unique_ptr<ProcessBreakpoint>> breakpoints_;

  // Each watchpoint holds the information about what range of addresses
  // it spans.
  std::map<debug_ipc::AddressRange, std::unique_ptr<ProcessWatchpoint>,
           debug_ipc::AddressRangeCompare>
      watchpoints_;

  debug_ipc::BufferedZxSocket stdout_;
  debug_ipc::BufferedZxSocket stderr_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedProcess);
};

}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_DEBUGGED_PROCESS_H_
