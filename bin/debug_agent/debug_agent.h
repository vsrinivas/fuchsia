// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_DEBUG_AGENT_H_
#define GARNET_BIN_DEBUG_AGENT_DEBUG_AGENT_H_

#include <zircon/types.h>
#include <map>
#include <memory>
#include "garnet/bin/debug_agent/breakpoint.h"
#include "garnet/bin/debug_agent/debugged_process.h"
#include "garnet/bin/debug_agent/remote_api.h"
#include "garnet/lib/debug_ipc/helper/stream_buffer.h"
#include "lib/fxl/macros.h"

namespace debug_agent {

// Main state and control for the debug agent.
class DebugAgent : public RemoteAPI, public Breakpoint::ProcessDelegate {
 public:
  // A MessageLoopZircon should already be set up on the current thread.
  //
  // The stream must outlive this class. It will be used to send data to the
  // client. It will not be read (that's the job of the provider of the
  // RemoteAPI).
  explicit DebugAgent(debug_ipc::StreamBuffer* stream);
  ~DebugAgent();

  debug_ipc::StreamBuffer* stream() { return stream_; }

  void RemoveDebuggedProcess(zx_koid_t process_koid);

  void RemoveBreakpoint(uint32_t breakpoint_id);

 private:
  // RemoteAPI implementation.
  void OnHello(const debug_ipc::HelloRequest& request,
               debug_ipc::HelloReply* reply) override;
  void OnLaunch(const debug_ipc::LaunchRequest& request,
                debug_ipc::LaunchReply* reply) override;
  void OnKill(const debug_ipc::KillRequest& request,
              debug_ipc::KillReply* reply) override;
  void OnAttach(std::vector<char> serialized) override;
  void OnDetach(const debug_ipc::DetachRequest& request,
                debug_ipc::DetachReply* reply) override;
  void OnPause(const debug_ipc::PauseRequest& request,
               debug_ipc::PauseReply* reply) override;
  void OnResume(const debug_ipc::ResumeRequest& request,
                debug_ipc::ResumeReply* reply) override;
  void OnModules(const debug_ipc::ModulesRequest& request,
                 debug_ipc::ModulesReply* reply) override;
  void OnProcessTree(const debug_ipc::ProcessTreeRequest& request,
                     debug_ipc::ProcessTreeReply* reply) override;
  void OnThreads(const debug_ipc::ThreadsRequest& request,
                 debug_ipc::ThreadsReply* reply) override;
  void OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                    debug_ipc::ReadMemoryReply* reply) override;
  void OnRegisters(const debug_ipc::RegistersRequest& request,
                   debug_ipc::RegistersReply* reply) override;
  void OnAddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      debug_ipc::AddOrChangeBreakpointReply* reply) override;
  void OnRemoveBreakpoint(const debug_ipc::RemoveBreakpointRequest& request,
                          debug_ipc::RemoveBreakpointReply* reply) override;
  void OnBacktrace(const debug_ipc::BacktraceRequest& request,
                   debug_ipc::BacktraceReply* reply) override;
  void OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                      debug_ipc::AddressSpaceReply* reply) override;

  // Breakpoint::ProcessDelegate implementation.
  zx_status_t RegisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                 uint64_t address) override;
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                            uint64_t address) override;

  // Returns the debugged process/thread for the given koid(s) or null if not
  // found.
  DebuggedProcess* GetDebuggedProcess(zx_koid_t koid);
  DebuggedThread* GetDebuggedThread(zx_koid_t process_koid,
                                    zx_koid_t thread_koid);

  // Returns a pointer to the newly created object.
  DebuggedProcess* AddDebuggedProcess(zx_koid_t process_koid,
                                      zx::process zx_proc);

  debug_ipc::StreamBuffer* stream_;

  std::map<zx_koid_t, std::unique_ptr<DebuggedProcess>> procs_;

  std::map<uint32_t, Breakpoint> breakpoints_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebugAgent);
};

}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_DEBUG_AGENT_H_
