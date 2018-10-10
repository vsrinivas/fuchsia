// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/minidump_remote_api.h"

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/lib/debug_ipc/client_protocol.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"
#include "third_party/crashpad/snapshot/minidump/process_snapshot_minidump.h"

namespace zxdb {

namespace {

Err ErrNoLive() {
  return Err(ErrType::kNoConnection, "System is no longer live");
}

Err ErrNoImpl() { return Err("Feature not implemented for minidump"); }

template <typename ReplyType>
void ErrNoLive(std::function<void(const Err&, ReplyType)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
    [cb]() { cb(ErrNoLive(), ReplyType()); });
}

template <typename ReplyType>
void ErrNoImpl(std::function<void(const Err&, ReplyType)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
    [cb]() { cb(ErrNoImpl(), ReplyType()); });
}

template <typename ReplyType>
void Succeed(std::function<void(const Err&, ReplyType)> cb, ReplyType r) {
  debug_ipc::MessageLoop::Current()->PostTask(
    [cb, r]() { cb(Err(), r); });
}

}  // namespace

MinidumpRemoteAPI::MinidumpRemoteAPI() = default;
MinidumpRemoteAPI::~MinidumpRemoteAPI() = default;

Err MinidumpRemoteAPI::Open(const std::string& path) {
  crashpad::FileReader reader;

  if (minidump_) {
    return Err("Dump already open");
  }

  if (!reader.Open(base::FilePath(path))) {
    return Err(fxl::StringPrintf("Could not open %s", path.c_str()));
  }

  minidump_ = std::make_unique<crashpad::ProcessSnapshotMinidump>();
  bool success = minidump_->Initialize(&reader);
  reader.Close();

  if (!success) {
    minidump_.release();
    return Err(fxl::StringPrintf("Minidump %s not valid", path.c_str()));
  }

  return Err();
}

Err MinidumpRemoteAPI::Close() {
  if (!minidump_) {
    return Err("No open dump to close");
  }

  minidump_.reset();
  return Err();
}

void MinidumpRemoteAPI::Hello(
    const debug_ipc::HelloRequest& request,
    std::function<void(const Err&, debug_ipc::HelloReply)> cb) {
  Succeed(cb, debug_ipc::HelloReply());
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
