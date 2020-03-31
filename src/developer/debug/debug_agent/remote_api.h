// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_REMOTE_API_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_REMOTE_API_H_

#include "src/developer/debug/ipc/protocol.h"

namespace debug_agent {

// This is an abstract class that implements calls corresponding to the
// client->agent IPC requests.
class RemoteAPI {
 public:
  RemoteAPI() {}
  virtual ~RemoteAPI() {}

  virtual void OnHello(const debug_ipc::HelloRequest& request, debug_ipc::HelloReply* reply) = 0;

  virtual void OnStatus(const debug_ipc::StatusRequest& request, debug_ipc::StatusReply* reply) = 0;

  virtual void OnLaunch(const debug_ipc::LaunchRequest& request, debug_ipc::LaunchReply* reply) = 0;
  virtual void OnKill(const debug_ipc::KillRequest& request, debug_ipc::KillReply* reply) = 0;

  virtual void OnConfigAgent(const debug_ipc::ConfigAgentRequest& request,
                             debug_ipc::ConfigAgentReply* reply) = 0;

  // Attach is special because it needs to follow the reply immediately with
  // a series of notifications about the current threads. This means it
  // can't use the automatic reply sending. It must manually deserialize and
  // send the reply.
  virtual void OnAttach(std::vector<char> serialized) = 0;
  // This is an overload with the result of reading |serialized|.
  // We have this so it's easier to call a MockRemoteAPI.
  virtual void OnAttach(uint32_t transaction_id, const debug_ipc::AttachRequest&) = 0;

  virtual void OnDetach(const debug_ipc::DetachRequest& request, debug_ipc::DetachReply* reply) = 0;

  virtual void OnModules(const debug_ipc::ModulesRequest& request,
                         debug_ipc::ModulesReply* reply) = 0;

  virtual void OnPause(const debug_ipc::PauseRequest& request, debug_ipc::PauseReply* reply) = 0;

  virtual void OnQuitAgent(const debug_ipc::QuitAgentRequest& request,
                           debug_ipc::QuitAgentReply* reply) = 0;

  virtual void OnResume(const debug_ipc::ResumeRequest& request, debug_ipc::ResumeReply* reply) = 0;

  virtual void OnProcessTree(const debug_ipc::ProcessTreeRequest& request,
                             debug_ipc::ProcessTreeReply* reply) = 0;

  virtual void OnThreads(const debug_ipc::ThreadsRequest& request,
                         debug_ipc::ThreadsReply* reply) = 0;

  virtual void OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                            debug_ipc::ReadMemoryReply* reply) = 0;

  virtual void OnReadRegisters(const debug_ipc::ReadRegistersRequest& request,
                               debug_ipc::ReadRegistersReply* reply) = 0;

  virtual void OnWriteRegisters(const debug_ipc::WriteRegistersRequest& request,
                                debug_ipc::WriteRegistersReply* reply) = 0;

  virtual void OnAddOrChangeBreakpoint(const debug_ipc::AddOrChangeBreakpointRequest& request,
                                       debug_ipc::AddOrChangeBreakpointReply* reply) = 0;

  virtual void OnRemoveBreakpoint(const debug_ipc::RemoveBreakpointRequest& request,
                                  debug_ipc::RemoveBreakpointReply* reply) = 0;

  virtual void OnSysInfo(const debug_ipc::SysInfoRequest& request,
                         debug_ipc::SysInfoReply* reply) = 0;

  virtual void OnProcessStatus(const debug_ipc::ProcessStatusRequest& request,
                               debug_ipc::ProcessStatusReply* reply) = 0;

  virtual void OnThreadStatus(const debug_ipc::ThreadStatusRequest& request,
                              debug_ipc::ThreadStatusReply* reply) = 0;

  virtual void OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                              debug_ipc::AddressSpaceReply* reply) = 0;

  virtual void OnJobFilter(const debug_ipc::JobFilterRequest& request,
                           debug_ipc::JobFilterReply* reply) = 0;

  virtual void OnWriteMemory(const debug_ipc::WriteMemoryRequest& request,
                             debug_ipc::WriteMemoryReply* reply) = 0;

  virtual void OnLoadInfoHandleTable(const debug_ipc::LoadInfoHandleTableRequest& request,
                                     debug_ipc::LoadInfoHandleTableReply* reply) = 0;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_REMOTE_API_H_
