// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ZXDB_CLIENT_REMOTE_API_H_
#define GARNET_BIN_ZXDB_CLIENT_REMOTE_API_H_

#include <functional>

#include "src/developer/debug/ipc/protocol.h"
#include "src/lib/fxl/macros.h"

namespace zxdb {

class Err;

// Abstracts the IPC layer for sending messages to the debug agent. This allows
// mocking of the interface without dealing with the innards of the
// serialization.
//
// The default implementations of each of these functions asserts. The Session
// implements overrides that actually send and receive messages. Tests should
// derive from this and implement the messages they expect.
class RemoteAPI {
 public:
  RemoteAPI() = default;
  virtual ~RemoteAPI() = default;

  virtual void Hello(const debug_ipc::HelloRequest& request,
                     std::function<void(const Err&, debug_ipc::HelloReply)> cb);
  virtual void Launch(
      const debug_ipc::LaunchRequest& request,
      std::function<void(const Err&, debug_ipc::LaunchReply)> cb);
  virtual void Kill(const debug_ipc::KillRequest& request,
                    std::function<void(const Err&, debug_ipc::KillReply)> cb);
  virtual void Attach(
      const debug_ipc::AttachRequest& request,
      std::function<void(const Err&, debug_ipc::AttachReply)> cb);
  virtual void ConfigAgent(
      const debug_ipc::ConfigAgentRequest& request,
      std::function<void(const Err&, debug_ipc::ConfigAgentReply)> cb);
  virtual void Detach(
      const debug_ipc::DetachRequest& request,
      std::function<void(const Err&, debug_ipc::DetachReply)> cb);
  virtual void Modules(
      const debug_ipc::ModulesRequest& request,
      std::function<void(const Err&, debug_ipc::ModulesReply)> cb);
  virtual void Pause(const debug_ipc::PauseRequest& request,
                     std::function<void(const Err&, debug_ipc::PauseReply)> cb);
  virtual void QuitAgent(
      const debug_ipc::QuitAgentRequest& request,
      std::function<void(const Err&, debug_ipc::QuitAgentReply)> cb);
  virtual void Resume(
      const debug_ipc::ResumeRequest& request,
      std::function<void(const Err&, debug_ipc::ResumeReply)> cb);
  virtual void ProcessTree(
      const debug_ipc::ProcessTreeRequest& request,
      std::function<void(const Err&, debug_ipc::ProcessTreeReply)> cb);
  virtual void Threads(
      const debug_ipc::ThreadsRequest& request,
      std::function<void(const Err&, debug_ipc::ThreadsReply)> cb);
  virtual void ReadMemory(
      const debug_ipc::ReadMemoryRequest& request,
      std::function<void(const Err&, debug_ipc::ReadMemoryReply)> cb);
  virtual void ReadRegisters(
      const debug_ipc::ReadRegistersRequest& request,
      std::function<void(const Err&, debug_ipc::ReadRegistersReply)> cb);
  virtual void WriteRegisters(
      const debug_ipc::WriteRegistersRequest& request,
      std::function<void(const Err&, debug_ipc::WriteRegistersReply)> cb);
  virtual void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)>
          cb);
  virtual void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb);
  virtual void SysInfo(
      const debug_ipc::SysInfoRequest& request,
      std::function<void(const Err&, debug_ipc::SysInfoReply)> cb);
  virtual void ThreadStatus(
      const debug_ipc::ThreadStatusRequest& request,
      std::function<void(const Err&, debug_ipc::ThreadStatusReply)> cb);
  virtual void AddressSpace(
      const debug_ipc::AddressSpaceRequest& request,
      std::function<void(const Err&, debug_ipc::AddressSpaceReply)> cb);
  virtual void JobFilter(
      const debug_ipc::JobFilterRequest& request,
      std::function<void(const Err&, debug_ipc::JobFilterReply)> cb);
  virtual void WriteMemory(
      const debug_ipc::WriteMemoryRequest& request,
      std::function<void(const Err&, debug_ipc::WriteMemoryReply)> cb);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteAPI);
};

}  // namespace zxdb

#endif  // GARNET_BIN_ZXDB_CLIENT_REMOTE_API_H_
