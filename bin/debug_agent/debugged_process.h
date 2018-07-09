// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_DEBUGGED_PROCESS_H_
#define GARNET_BIN_DEBUG_AGENT_DEBUGGED_PROCESS_H_

#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <map>
#include <memory>
#include <vector>

#include "garnet/bin/debug_agent/process_memory_accessor.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/helper/zircon_exception_watcher.h"
#include "garnet/lib/debug_ipc/protocol.h"

#include "lib/fxl/macros.h"

namespace debug_agent {

class Breakpoint;
class DebugAgent;
class DebuggedThread;
class ProcessBreakpoint;

class DebuggedProcess : public debug_ipc::ZirconExceptionWatcher,
                        public ProcessMemoryAccessor {
 public:
  // Caller must call Init immediately after construction and delete the
  // object if that fails.
  DebuggedProcess(DebugAgent* debug_agent, zx_koid_t process_koid,
                  zx::process proc);
  virtual ~DebuggedProcess();

  zx_koid_t koid() const { return koid_; }
  DebugAgent* debug_agent() const { return debug_agent_; }
  zx::process& process() { return process_; }
  uint64_t dl_debug_addr() const { return dl_debug_addr_; }

  // Returns true on success. On failure, the object may not be used further.
  bool Init();

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

  // Returns the thread or null if there is no known thread for this koid.
  DebuggedThread* GetThread(zx_koid_t thread_koid);

  // Populates the thread map with the current threads for this process, and
  // sends the list to the client. Used after an attach where we will not get
  // new thread notifications.
  void PopulateCurrentThreads();

  // Attempts to load the debug_state_ value from the
  // ZX_PROP_PROCESS_DEBUG_ADDR of the debugged process. Returns true if it
  // is now set. False means it remains unset.
  bool RegisterDebugState();

  // Looks for breakpoints at the given address. Null if no breakpoints are
  // at that address.
  ProcessBreakpoint* FindProcessBreakpointForAddr(uint64_t address);

  // Notifications when breakpoints are added or removed that affect this
  // process.
  zx_status_t RegisterBreakpoint(Breakpoint* bp, uint64_t address);
  void UnregisterBreakpoint(Breakpoint* bp, uint64_t address);

 private:
  // ZirconExceptionWatcher implementation.
  void OnProcessTerminated(zx_koid_t process_koid) override;
  void OnThreadStarting(zx_koid_t process_koid, zx_koid_t thread_koid) override;
  void OnThreadExiting(zx_koid_t process_koid, zx_koid_t thread_koid) override;
  void OnException(zx_koid_t process_koid, zx_koid_t thread_koid,
                   uint32_t type) override;

  // ProcessMemoryAccessor implementation.
  zx_status_t ReadProcessMemory(uintptr_t address, void* buffer, size_t len,
                                size_t* actual) override;
  zx_status_t WriteProcessMemory(uintptr_t address, const void* buffer,
                                 size_t len, size_t* actual) override;

  DebugAgent* debug_agent_;  // Non-owning.
  zx_koid_t koid_;
  zx::process process_;

  // Address in the debugged program of the dl_debug_state in ld.so.
  uint64_t dl_debug_addr_ = 0;

  // Handle for watching the process exceptions.
  debug_ipc::MessageLoop::WatchHandle process_watch_handle_;

  std::map<zx_koid_t, std::unique_ptr<DebuggedThread>> threads_;

  // Maps addresses to the ProcessBreakpoint at a location. The
  // ProcessBreakpoint can hold multiple Breakpoint objects.
  std::map<uint64_t, std::unique_ptr<ProcessBreakpoint>> breakpoints_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedProcess);
};

}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_DEBUGGED_PROCESS_H_
