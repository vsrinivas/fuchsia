// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/debug_agent/debugged_process.h"
#include "garnet/bin/debug_agent/exception_handler.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/public/lib/fxl/macros.h"

// Main state and control for the debug agent. The exception handler reports
// exceptions in the debugged program directly to this class and data from
// the debugger client via a StreamBuffer.
//
// This class sends data back to the client via the same StreamBuffer.
class DebugAgent : public ExceptionHandler::Sink {
 public:
  // The exception handler is non-owning and must outlive this class.
  explicit DebugAgent(ExceptionHandler* handler);
  ~DebugAgent();

  debug_ipc::StreamBuffer& stream() { return handler_->socket_buffer(); }

  // ExceptionHandler::Sink implementation.
  void OnStreamData() override;
  void OnProcessTerminated(zx_koid_t process_koid) override;
  void OnThreadStarting(const zx::thread& thread, zx_koid_t proc_koid,
                        zx_koid_t thread_koid) override;
  void OnThreadExiting(const zx::thread& thread, zx_koid_t proc_koid,
                        zx_koid_t thread_koid) override;

 private:
  // IPC handlers.
  void OnHello(const debug_ipc::HelloRequest& request,
               debug_ipc::HelloReply* reply);
  void OnLaunch(const debug_ipc::LaunchRequest& request,
                debug_ipc::LaunchReply* reply);
  void OnProcessTree(const debug_ipc::ProcessTreeRequest& request,
                     debug_ipc::ProcessTreeReply* reply);
  void OnThreads(const debug_ipc::ThreadsRequest& request,
                 debug_ipc::ThreadsReply* reply);
  void OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                    debug_ipc::ReadMemoryReply* reply);

  void AddDebuggedProcess(zx_koid_t koid, zx::process proc);
  void RemoveDebuggedProcess(zx_koid_t koid);

  ExceptionHandler* handler_;  // Non-owning.

  std::map<zx_koid_t, DebuggedProcess> procs_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebugAgent);
};
