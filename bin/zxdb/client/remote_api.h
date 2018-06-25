// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/public/lib/fxl/macros.h"

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
  virtual void Detach(
      const debug_ipc::DetachRequest& request,
      std::function<void(const Err&, debug_ipc::DetachReply)> cb);
  virtual void Modules(
      const debug_ipc::ModulesRequest& request,
      std::function<void(const Err&, debug_ipc::ModulesReply)> cb);
  virtual void Pause(const debug_ipc::PauseRequest& request,
                     std::function<void(const Err&, debug_ipc::PauseReply)> cb);
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
  virtual void Registers(
      const debug_ipc::RegistersRequest& request,
      std::function<void(const Err&, debug_ipc::RegistersReply)> cb);
  virtual void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)>
          cb);
  virtual void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb);
  virtual void Backtrace(
      const debug_ipc::BacktraceRequest& request,
      std::function<void(const Err&, debug_ipc::BacktraceReply)> cb);
  virtual void AddressSpace(
      const debug_ipc::AddressSpaceRequest& request,
      std::function<void(const Err&, debug_ipc::AddressSpaceReply)> cb);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteAPI);
};

}  // namespace zxdb
