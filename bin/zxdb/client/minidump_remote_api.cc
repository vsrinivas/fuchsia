// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/minidump_remote_api.h"

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/lib/debug_ipc/client_protocol.h"

namespace zxdb {

namespace {

Err ErrNoLive() {
  return Err(ErrType::kNoConnection, "System is no longer live");
}

Err ErrNoImpl() { return Err("Feature not implemented for minidump"); }

template <typename ReplyType>
void ErrNoLive(std::function<void(const Err&, ReplyType)> cb) {
  cb(ErrNoLive(), ReplyType());
}

template <typename ReplyType>
void ErrNoImpl(std::function<void(const Err&, ReplyType)> cb) {
  cb(ErrNoImpl(), ReplyType());
}

}  // namespace

MinidumpRemoteAPI::MinidumpRemoteAPI(const std::string& path) {}

void MinidumpRemoteAPI::Hello(
    const debug_ipc::HelloRequest& request,
    std::function<void(const Err&, debug_ipc::HelloReply)> cb) {
  cb(Err(), debug_ipc::HelloReply());
}

void MinidumpRemoteAPI::Launch(
    const debug_ipc::LaunchRequest& request,
    std::function<void(const Err&, debug_ipc::LaunchReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::Kill(
    const debug_ipc::KillRequest& request,
    std::function<void(const Err&, debug_ipc::KillReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::Attach(
    const debug_ipc::AttachRequest& request,
    std::function<void(const Err&, debug_ipc::AttachReply)> cb) {
  // TODO
  ErrNoImpl(cb);
}

void MinidumpRemoteAPI::Detach(
    const debug_ipc::DetachRequest& request,
    std::function<void(const Err&, debug_ipc::DetachReply)> cb) {
  // TODO
  ErrNoImpl(cb);
}

void MinidumpRemoteAPI::Modules(
    const debug_ipc::ModulesRequest& request,
    std::function<void(const Err&, debug_ipc::ModulesReply)> cb) {
  // TODO
  ErrNoImpl(cb);
}

void MinidumpRemoteAPI::Pause(
    const debug_ipc::PauseRequest& request,
    std::function<void(const Err&, debug_ipc::PauseReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::Resume(
    const debug_ipc::ResumeRequest& request,
    std::function<void(const Err&, debug_ipc::ResumeReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::ProcessTree(
    const debug_ipc::ProcessTreeRequest& request,
    std::function<void(const Err&, debug_ipc::ProcessTreeReply)> cb) {
  // TODO
  ErrNoImpl(cb);
}

void MinidumpRemoteAPI::Threads(
    const debug_ipc::ThreadsRequest& request,
    std::function<void(const Err&, debug_ipc::ThreadsReply)> cb) {
  // TODO
  ErrNoImpl(cb);
}

void MinidumpRemoteAPI::ReadMemory(
    const debug_ipc::ReadMemoryRequest& request,
    std::function<void(const Err&, debug_ipc::ReadMemoryReply)> cb) {
  // TODO
  ErrNoImpl(cb);
}

void MinidumpRemoteAPI::Registers(
    const debug_ipc::RegistersRequest& request,
    std::function<void(const Err&, debug_ipc::RegistersReply)> cb) {
  // TODO
  ErrNoImpl(cb);
}

void MinidumpRemoteAPI::AddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::RemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::Backtrace(
    const debug_ipc::BacktraceRequest& request,
    std::function<void(const Err&, debug_ipc::BacktraceReply)> cb) {
  // TODO
  ErrNoImpl(cb);
}

void MinidumpRemoteAPI::AddressSpace(
    const debug_ipc::AddressSpaceRequest& request,
    std::function<void(const Err&, debug_ipc::AddressSpaceReply)> cb) {
  // TODO
  ErrNoImpl(cb);
}

}  // namespace zxdb
