// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>
#include <zx/process.h>
#include <zx/thread.h>

#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/public/lib/fxl/macros.h"

namespace debug_agent {

class DebugAgent;
class DebuggedThread;
class ProcessBreakpoint;

class DebuggedProcess {
 public:
  DebuggedProcess(DebugAgent* debug_agent, zx_koid_t koid, zx::process proc);
  ~DebuggedProcess();

  zx_koid_t koid() const { return koid_; }
  DebugAgent* debug_agent() const { return debug_agent_; }
  zx::process& process() { return process_; }

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

  void OnThreadStarting(zx::thread thread, zx_koid_t thread_koid);
  void OnThreadExiting(zx_koid_t thread_koid);

  // Populates the thread map with the current threads for this process. Used
  // after an attach where we will not get new thread notifications.
  void PopulateCurrentThreads();

  // Notification that an exception has happened on the given thread. The
  // thread will be in a "suspended on exception" state.
  void OnException(zx_koid_t thread_koid, uint32_t type);

  // Looks for a breakpoint that could have generated a software breakpoint
  // at the given address. Returns null if none found.
  ProcessBreakpoint* FindBreakpointForAddr(uint64_t address);

 private:
  DebugAgent* debug_agent_;  // Non-owning.
  zx_koid_t koid_;
  zx::process process_;

  std::map<zx_koid_t, std::unique_ptr<DebuggedThread>> threads_;

  // List of breakpoints indexed by IDs (IDs are assigned by the client).
  std::map<uint32_t, std::unique_ptr<ProcessBreakpoint>> breakpoints_;

  // Maps the address of each breakpoint to the unique ID used by the
  // breakpoints_ map.
  std::map<uint64_t, uint32_t> address_to_breakpoint_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedProcess);
};

}  // namespace debug_agent
