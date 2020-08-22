// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/remote_api.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/client/session.h"

namespace zxdb {

void RemoteAPI::Hello(const debug_ipc::HelloRequest& request,
                      fit::callback<void(const Err&, debug_ipc::HelloReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::Launch(const debug_ipc::LaunchRequest& request,
                       fit::callback<void(const Err&, debug_ipc::LaunchReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::Kill(const debug_ipc::KillRequest& request,
                     fit::callback<void(const Err&, debug_ipc::KillReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::Attach(const debug_ipc::AttachRequest& request,
                       fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::ConfigAgent(const debug_ipc::ConfigAgentRequest& request,
                            fit::callback<void(const Err&, debug_ipc::ConfigAgentReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::Detach(const debug_ipc::DetachRequest& request,
                       fit::callback<void(const Err&, debug_ipc::DetachReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::Modules(const debug_ipc::ModulesRequest& request,
                        fit::callback<void(const Err&, debug_ipc::ModulesReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::Pause(const debug_ipc::PauseRequest& request,
                      fit::callback<void(const Err&, debug_ipc::PauseReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::QuitAgent(const debug_ipc::QuitAgentRequest& request,
                          fit::callback<void(const Err&, debug_ipc::QuitAgentReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::Resume(const debug_ipc::ResumeRequest& request,
                       fit::callback<void(const Err&, debug_ipc::ResumeReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::ProcessTree(const debug_ipc::ProcessTreeRequest& request,
                            fit::callback<void(const Err&, debug_ipc::ProcessTreeReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::Threads(const debug_ipc::ThreadsRequest& request,
                        fit::callback<void(const Err&, debug_ipc::ThreadsReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::ReadMemory(const debug_ipc::ReadMemoryRequest& request,
                           fit::callback<void(const Err&, debug_ipc::ReadMemoryReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::ReadRegisters(const debug_ipc::ReadRegistersRequest& request,
                              fit::callback<void(const Err&, debug_ipc::ReadRegistersReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::WriteRegisters(const debug_ipc::WriteRegistersRequest& request,
                               fit::callback<void(const Err&, debug_ipc::WriteRegistersReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::AddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    fit::callback<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::RemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    fit::callback<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::SysInfo(const debug_ipc::SysInfoRequest& request,
                        fit::callback<void(const Err&, debug_ipc::SysInfoReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::Status(const debug_ipc::StatusRequest& request,
                       fit::callback<void(const Err&, debug_ipc::StatusReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::ProcessStatus(const debug_ipc::ProcessStatusRequest& request,
                              fit::callback<void(const Err&, debug_ipc::ProcessStatusReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::ThreadStatus(const debug_ipc::ThreadStatusRequest& request,
                             fit::callback<void(const Err&, debug_ipc::ThreadStatusReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::AddressSpace(const debug_ipc::AddressSpaceRequest& request,
                             fit::callback<void(const Err&, debug_ipc::AddressSpaceReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::JobFilter(const debug_ipc::JobFilterRequest& request,
                          fit::callback<void(const Err&, debug_ipc::JobFilterReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::WriteMemory(const debug_ipc::WriteMemoryRequest& request,
                            fit::callback<void(const Err&, debug_ipc::WriteMemoryReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::LoadInfoHandleTable(
    const debug_ipc::LoadInfoHandleTableRequest& request,
    fit::callback<void(const Err&, debug_ipc::LoadInfoHandleTableReply)> cb) {
  FX_NOTREACHED();
}

void RemoteAPI::UpdateGlobalSettings(
    const debug_ipc::UpdateGlobalSettingsRequest& request,
    fit::callback<void(const Err&, debug_ipc::UpdateGlobalSettingsReply)> cb) {
  FX_NOTREACHED();
}

}  // namespace zxdb
