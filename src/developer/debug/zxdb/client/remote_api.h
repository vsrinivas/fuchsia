// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_H_

#include "lib/fit/function.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/lib/fxl/macros.h"

namespace zxdb {

class Err;

// Abstracts the IPC layer for sending messages to the debug agent. This allows mocking of the
// interface without dealing with the innards of the serialization.
//
// The default implementations of each of these functions asserts. The Session implements overrides
// that actually send and receive messages. Tests should derive from this and implement the messages
// they expect.
class RemoteAPI {
 public:
  RemoteAPI() = default;
  virtual ~RemoteAPI() = default;

  virtual void Hello(const debug_ipc::HelloRequest& request,
                     fit::callback<void(const Err&, debug_ipc::HelloReply)> cb);
  virtual void Launch(const debug_ipc::LaunchRequest& request,
                      fit::callback<void(const Err&, debug_ipc::LaunchReply)> cb);
  virtual void Kill(const debug_ipc::KillRequest& request,
                    fit::callback<void(const Err&, debug_ipc::KillReply)> cb);
  virtual void Attach(const debug_ipc::AttachRequest& request,
                      fit::callback<void(const Err&, debug_ipc::AttachReply)> cb);
  virtual void ConfigAgent(const debug_ipc::ConfigAgentRequest& request,
                           fit::callback<void(const Err&, debug_ipc::ConfigAgentReply)> cb);
  virtual void Detach(const debug_ipc::DetachRequest& request,
                      fit::callback<void(const Err&, debug_ipc::DetachReply)> cb);
  virtual void Modules(const debug_ipc::ModulesRequest& request,
                       fit::callback<void(const Err&, debug_ipc::ModulesReply)> cb);
  virtual void Pause(const debug_ipc::PauseRequest& request,
                     fit::callback<void(const Err&, debug_ipc::PauseReply)> cb);
  virtual void QuitAgent(const debug_ipc::QuitAgentRequest& request,
                         fit::callback<void(const Err&, debug_ipc::QuitAgentReply)> cb);
  virtual void Resume(const debug_ipc::ResumeRequest& request,
                      fit::callback<void(const Err&, debug_ipc::ResumeReply)> cb);
  virtual void ProcessTree(const debug_ipc::ProcessTreeRequest& request,
                           fit::callback<void(const Err&, debug_ipc::ProcessTreeReply)> cb);
  virtual void Threads(const debug_ipc::ThreadsRequest& request,
                       fit::callback<void(const Err&, debug_ipc::ThreadsReply)> cb);
  virtual void ReadMemory(const debug_ipc::ReadMemoryRequest& request,
                          fit::callback<void(const Err&, debug_ipc::ReadMemoryReply)> cb);
  virtual void ReadRegisters(const debug_ipc::ReadRegistersRequest& request,
                             fit::callback<void(const Err&, debug_ipc::ReadRegistersReply)> cb);
  virtual void WriteRegisters(const debug_ipc::WriteRegistersRequest& request,
                              fit::callback<void(const Err&, debug_ipc::WriteRegistersReply)> cb);
  virtual void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      fit::callback<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb);
  virtual void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      fit::callback<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb);
  virtual void SysInfo(const debug_ipc::SysInfoRequest& request,
                       fit::callback<void(const Err&, debug_ipc::SysInfoReply)> cb);
  virtual void Status(const debug_ipc::StatusRequest& request,
                      fit::callback<void(const Err&, debug_ipc::StatusReply)> cb);
  virtual void ProcessStatus(const debug_ipc::ProcessStatusRequest& request,
                             fit::callback<void(const Err&, debug_ipc::ProcessStatusReply)> cb);
  virtual void ThreadStatus(const debug_ipc::ThreadStatusRequest& request,
                            fit::callback<void(const Err&, debug_ipc::ThreadStatusReply)> cb);
  virtual void AddressSpace(const debug_ipc::AddressSpaceRequest& request,
                            fit::callback<void(const Err&, debug_ipc::AddressSpaceReply)> cb);
  virtual void JobFilter(const debug_ipc::JobFilterRequest& request,
                         fit::callback<void(const Err&, debug_ipc::JobFilterReply)> cb);
  virtual void WriteMemory(const debug_ipc::WriteMemoryRequest& request,
                           fit::callback<void(const Err&, debug_ipc::WriteMemoryReply)> cb);
  virtual void LoadInfoHandleTable(
      const debug_ipc::LoadInfoHandleTableRequest& request,
      fit::callback<void(const Err&, debug_ipc::LoadInfoHandleTableReply)> cb);
  virtual void UpdateGlobalSettings(
      const debug_ipc::UpdateGlobalSettingsRequest& request,
      fit::callback<void(const Err&, debug_ipc::UpdateGlobalSettingsReply)> cb);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteAPI);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_H_
