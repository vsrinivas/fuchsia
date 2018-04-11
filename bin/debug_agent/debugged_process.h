// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <map>
#include <memory>

#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/helper/zircon_exception_watcher.h"
#include "garnet/public/lib/fxl/macros.h"

namespace debug_agent {

class DebugAgent;
class DebuggedThread;
class ProcessBreakpoint;

class DebuggedProcess : public debug_ipc::ZirconExceptionWatcher {
 public:
  // Caller must call Init immediately after construction and delete the
  // object if that fails.
  DebuggedProcess(DebugAgent* debug_agent,
                  zx_koid_t process_koid,
                  zx::process proc);
  virtual ~DebuggedProcess();

  zx_koid_t koid() const { return koid_; }
  DebugAgent* debug_agent() const { return debug_agent_; }
  zx::process& process() { return process_; }

  // Returns true on success. On failure, the object may not be used further.
  bool Init();

  // IPC handlers.
  void OnPause(const debug_ipc::PauseRequest& request);
  void OnResume(const debug_ipc::ResumeRequest& request);
  void OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                    debug_ipc::ReadMemoryReply* reply);
  void OnAddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      debug_ipc::AddOrChangeBreakpointReply* reply);
  void OnRemoveBreakpoint(const debug_ipc::RemoveBreakpointRequest& request,
                          debug_ipc::RemoveBreakpointReply* reply);
  void OnKill(const debug_ipc::KillRequest& request,
              debug_ipc::KillReply* reply);

  // Returns the thread or null if there is no known thread for this koid.
  DebuggedThread* GetThread(zx_koid_t thread_koid);

  // Populates the thread map with the current threads for this process, and
  // sends the list to the client. Used after an attach where we will not get
  // new thread notifications.
  void PopulateCurrentThreads();

  // Looks for a breakpoint that could have generated a software breakpoint
  // at the given address. Returns null if none found.
  ProcessBreakpoint* FindBreakpointForAddr(uint64_t address);

 private:
  // ZirconExceptionWatcher implementation.
  void OnProcessTerminated(zx_koid_t process_koid) override;
  void OnThreadStarting(zx_koid_t process_koid,
                        zx_koid_t thread_koid) override;
  void OnThreadExiting(zx_koid_t process_koid,
                       zx_koid_t thread_koid) override;
  void OnException(zx_koid_t process_koid,
                   zx_koid_t thread_koid,
                   uint32_t type) override;

  DebugAgent* debug_agent_;  // Non-owning.
  zx_koid_t koid_;
  zx::process process_;

  // Handle for watching the process exceptions.
  debug_ipc::MessageLoop::WatchHandle process_watch_handle_;

  std::map<zx_koid_t, std::unique_ptr<DebuggedThread>> threads_;

  // List of breakpoints indexed by IDs (IDs are assigned by the client).
  std::map<uint32_t, std::unique_ptr<ProcessBreakpoint>> breakpoints_;

  // Maps the address of each breakpoint to the unique ID used by the
  // breakpoints_ map.
  std::map<uint64_t, uint32_t> address_to_breakpoint_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedProcess);
};

}  // namespace debug_agent
