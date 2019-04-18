// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ZXDB_CLIENT_REMOTE_API_IMPL_H_
#define GARNET_BIN_ZXDB_CLIENT_REMOTE_API_IMPL_H_

#include "src/developer/debug/zxdb/client/remote_api.h"

namespace zxdb {

class Session;

// An implementation of RemoteAPI for Session. This class is logically part of
// the Session class (it's a friend) but is separated out for clarity.
class RemoteAPIImpl : public RemoteAPI {
 public:
  // The session must outlive this object.
  explicit RemoteAPIImpl(Session* session);
  ~RemoteAPIImpl();

  // RemoteAPI implementation.
  void Hello(
      const debug_ipc::HelloRequest& request,
      std::function<void(const Err&, debug_ipc::HelloReply)> cb) override;
  void Launch(
      const debug_ipc::LaunchRequest& request,
      std::function<void(const Err&, debug_ipc::LaunchReply)> cb) override;
  void Kill(const debug_ipc::KillRequest& request,
            std::function<void(const Err&, debug_ipc::KillReply)> cb) override;
  void Attach(
      const debug_ipc::AttachRequest& request,
      std::function<void(const Err&, debug_ipc::AttachReply)> cb) override;
  void ConfigAgent(
      const debug_ipc::ConfigAgentRequest& request,
      std::function<void(const Err&, debug_ipc::ConfigAgentReply)> cb) override;
  void Detach(
      const debug_ipc::DetachRequest& request,
      std::function<void(const Err&, debug_ipc::DetachReply)> cb) override;
  void Modules(
      const debug_ipc::ModulesRequest& request,
      std::function<void(const Err&, debug_ipc::ModulesReply)> cb) override;
  void Pause(
      const debug_ipc::PauseRequest& request,
      std::function<void(const Err&, debug_ipc::PauseReply)> cb) override;
  void QuitAgent(
      const debug_ipc::QuitAgentRequest& request,
      std::function<void(const Err&, debug_ipc::QuitAgentReply)> cb) override;
  void Resume(
      const debug_ipc::ResumeRequest& request,
      std::function<void(const Err&, debug_ipc::ResumeReply)> cb) override;
  void ProcessTree(
      const debug_ipc::ProcessTreeRequest& request,
      std::function<void(const Err&, debug_ipc::ProcessTreeReply)> cb) override;
  void Threads(
      const debug_ipc::ThreadsRequest& request,
      std::function<void(const Err&, debug_ipc::ThreadsReply)> cb) override;
  void ReadMemory(
      const debug_ipc::ReadMemoryRequest& request,
      std::function<void(const Err&, debug_ipc::ReadMemoryReply)> cb) override;
  void ReadRegisters(
      const debug_ipc::ReadRegistersRequest& request,
      std::function<void(const Err&, debug_ipc::ReadRegistersReply)> cb)
      override;
  void WriteRegisters(
      const debug_ipc::WriteRegistersRequest& request,
      std::function<void(const Err&, debug_ipc::WriteRegistersReply)> cb)
      override;
  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb)
      override;
  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb)
      override;
  void SysInfo(
      const debug_ipc::SysInfoRequest& request,
      std::function<void(const Err&, debug_ipc::SysInfoReply)> cb)
      override;
  void ThreadStatus(
      const debug_ipc::ThreadStatusRequest& request,
      std::function<void(const Err&, debug_ipc::ThreadStatusReply)> cb)
      override;
  void AddressSpace(
      const debug_ipc::AddressSpaceRequest& request,
      std::function<void(const Err&, debug_ipc::AddressSpaceReply)> cb)
      override;
  void JobFilter(
      const debug_ipc::JobFilterRequest& request,
      std::function<void(const Err&, debug_ipc::JobFilterReply)> cb) override;
  void WriteMemory(
      const debug_ipc::WriteMemoryRequest& request,
      std::function<void(const Err&, debug_ipc::WriteMemoryReply)> cb) override;

 private:
  // Sends a message with an asynchronous reply.
  //
  // The callback will be issued with an Err struct. If the Err object
  // indicates an error, the request has failed and the reply data will not be
  // set (it will contain the default-constructed data).
  //
  // The callback will always be issued asynchronously (not from withing the
  // Send() function itself).
  template <typename SendMsgType, typename RecvMsgType>
  void Send(const SendMsgType& send_msg,
            std::function<void(const Err&, RecvMsgType)> callback);

  Session* session_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteAPIImpl);
};

}  // namespace zxdb

#endif  // GARNET_BIN_ZXDB_CLIENT_REMOTE_API_IMPL_H_
