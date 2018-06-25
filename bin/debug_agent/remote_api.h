// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/protocol.h"

namespace debug_agent {

// This is an abstract class that implements calls corresponding to the
// client->agent IPC requests.
class RemoteAPI {
 public:
  RemoteAPI() {}
  virtual ~RemoteAPI() {}

  virtual void OnHello(const debug_ipc::HelloRequest& request,
                       debug_ipc::HelloReply* reply) = 0;

  virtual void OnLaunch(const debug_ipc::LaunchRequest& request,
                        debug_ipc::LaunchReply* reply) = 0;
  virtual void OnKill(const debug_ipc::KillRequest& request,
                      debug_ipc::KillReply* reply) = 0;

  // Attach is special because it needs to follow the reply immediately with
  // a series of notifications about the current threads. This means it
  // can't use the automatic reply sending. It must manually deserialize and
  // send the reply.
  virtual void OnAttach(std::vector<char> serialized) = 0;

  virtual void OnDetach(const debug_ipc::DetachRequest& request,
                        debug_ipc::DetachReply* reply) = 0;

  virtual void OnModules(const debug_ipc::ModulesRequest& request,
                         debug_ipc::ModulesReply* reply) = 0;

  virtual void OnPause(const debug_ipc::PauseRequest& request,
                       debug_ipc::PauseReply* reply) = 0;

  virtual void OnResume(const debug_ipc::ResumeRequest& request,
                        debug_ipc::ResumeReply* reply) = 0;

  virtual void OnProcessTree(const debug_ipc::ProcessTreeRequest& request,
                             debug_ipc::ProcessTreeReply* reply) = 0;

  virtual void OnThreads(const debug_ipc::ThreadsRequest& request,
                         debug_ipc::ThreadsReply* reply) = 0;

  virtual void OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                            debug_ipc::ReadMemoryReply* reply) = 0;

  virtual void OnRegisters(const debug_ipc::RegistersRequest& request,
                           debug_ipc::RegistersReply* reply) = 0;

  virtual void OnAddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      debug_ipc::AddOrChangeBreakpointReply* reply) = 0;

  virtual void OnRemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      debug_ipc::RemoveBreakpointReply* reply) = 0;

  virtual void OnBacktrace(const debug_ipc::BacktraceRequest& request,
                           debug_ipc::BacktraceReply* reply) = 0;

  virtual void OnAddressSpace(const debug_ipc::AddressSpaceRequest& request,
                              debug_ipc::AddressSpaceReply* reply) = 0;
};

}  // namespace debug_agent
