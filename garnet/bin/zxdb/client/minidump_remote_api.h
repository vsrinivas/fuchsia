// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/remote_api.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/session.h"

namespace crashpad {

class ProcessSnapshotMinidump;
class MemorySnapshot;

}  // namespace crashpad

namespace zxdb {

class Session;

// An implementation of RemoteAPI for Session that accesses a minidump file.
class MinidumpRemoteAPI : public RemoteAPI {
 public:
  explicit MinidumpRemoteAPI(Session* session);
  ~MinidumpRemoteAPI();

  Err Open(const std::string& path);
  Err Close();

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
  void Detach(
      const debug_ipc::DetachRequest& request,
      std::function<void(const Err&, debug_ipc::DetachReply)> cb) override;
  void Modules(
      const debug_ipc::ModulesRequest& request,
      std::function<void(const Err&, debug_ipc::ModulesReply)> cb) override;
  void Pause(
      const debug_ipc::PauseRequest& request,
      std::function<void(const Err&, debug_ipc::PauseReply)> cb) override;
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
  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb)
      override;
  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb)
      override;
  void ThreadStatus(
      const debug_ipc::ThreadStatusRequest& request,
      std::function<void(const Err&, debug_ipc::ThreadStatusReply)> cb)
      override;
  void AddressSpace(
      const debug_ipc::AddressSpaceRequest& request,
      std::function<void(const Err&, debug_ipc::AddressSpaceReply)> cb)
      override;
  virtual void JobFilter(
      const debug_ipc::JobFilterRequest& request,
      std::function<void(const Err&, debug_ipc::JobFilterReply)> cb) override;
  virtual void WriteMemory(
      const debug_ipc::WriteMemoryRequest& request,
      std::function<void(const Err&, debug_ipc::WriteMemoryReply)> cb) override;

  class MemoryRegion {
   public:
    MemoryRegion(uint64_t start_in, size_t size_in)
        : start(start_in), size(size_in) {}
    virtual ~MemoryRegion() = default;

    virtual std::optional<std::vector<uint8_t>> Read(uint64_t offset,
                                                     size_t size) const = 0;

    const uint64_t start;
    const size_t size;
  };

 private:

  // Initialization routine. Iterates minidump structures and finds all the
  // readable memory.
  void CollectMemory();

  std::string ProcessName();

  bool attached_ = false;
  Session* session_;

  std::vector<std::unique_ptr<MemoryRegion>> memory_;
  std::unique_ptr<crashpad::ProcessSnapshotMinidump> minidump_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MinidumpRemoteAPI);
};

}  // namespace zxdb
