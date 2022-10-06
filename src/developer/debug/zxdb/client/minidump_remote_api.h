// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MINIDUMP_REMOTE_API_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MINIDUMP_REMOTE_API_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/download_observer.h"
#include "src/developer/debug/zxdb/client/minidump_memory.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "third_party/crashpad/snapshot/cpu_context.h"
#include "third_party/crashpad/snapshot/memory_snapshot.h"
#include "third_party/crashpad/snapshot/minidump/process_snapshot_minidump.h"

namespace zxdb {

class Session;

// An implementation of RemoteAPI for Session that accesses a minidump file.
class MinidumpRemoteAPI : public RemoteAPI, public DownloadObserver {
 public:
  explicit MinidumpRemoteAPI(Session* session);
  ~MinidumpRemoteAPI();

  Err Open(const std::string& path);
  Err Close();

  // The process ID for the (presumably only) process in this dump.
  uint64_t ProcessID() { return minidump_->ProcessID(); }

  // RemoteAPI implementation.
  void Hello(const debug_ipc::HelloRequest& request,
             fit::callback<void(const Err&, debug_ipc::HelloReply)> cb) override;
  void Launch(const debug_ipc::LaunchRequest& request,
              fit::callback<void(const Err&, debug_ipc::LaunchReply)> cb) override;
  void Kill(const debug_ipc::KillRequest& request,
            fit::callback<void(const Err&, debug_ipc::KillReply)> cb) override;
  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) override;
  void Detach(const debug_ipc::DetachRequest& request,
              fit::callback<void(const Err&, debug_ipc::DetachReply)> cb) override;
  void Modules(const debug_ipc::ModulesRequest& request,
               fit::callback<void(const Err&, debug_ipc::ModulesReply)> cb) override;
  void Pause(const debug_ipc::PauseRequest& request,
             fit::callback<void(const Err&, debug_ipc::PauseReply)> cb) override;
  void Resume(const debug_ipc::ResumeRequest& request,
              fit::callback<void(const Err&, debug_ipc::ResumeReply)> cb) override;
  void ProcessTree(const debug_ipc::ProcessTreeRequest& request,
                   fit::callback<void(const Err&, debug_ipc::ProcessTreeReply)> cb) override;
  void Threads(const debug_ipc::ThreadsRequest& request,
               fit::callback<void(const Err&, debug_ipc::ThreadsReply)> cb) override;
  void ReadMemory(const debug_ipc::ReadMemoryRequest& request,
                  fit::callback<void(const Err&, debug_ipc::ReadMemoryReply)> cb) override;
  void ReadRegisters(const debug_ipc::ReadRegistersRequest& request,
                     fit::callback<void(const Err&, debug_ipc::ReadRegistersReply)> cb) override;
  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      fit::callback<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) override;
  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      fit::callback<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) override;
  void SysInfo(const debug_ipc::SysInfoRequest& request,
               fit::callback<void(const Err&, debug_ipc::SysInfoReply)> cb) override;
  void ThreadStatus(const debug_ipc::ThreadStatusRequest& request,
                    fit::callback<void(const Err&, debug_ipc::ThreadStatusReply)> cb) override;
  void AddressSpace(const debug_ipc::AddressSpaceRequest& request,
                    fit::callback<void(const Err&, debug_ipc::AddressSpaceReply)> cb) override;
  void UpdateFilter(const debug_ipc::UpdateFilterRequest& request,
                    fit::callback<void(const Err&, debug_ipc::UpdateFilterReply)> cb) override;
  void WriteMemory(const debug_ipc::WriteMemoryRequest& request,
                   fit::callback<void(const Err&, debug_ipc::WriteMemoryReply)> cb) override;
  void SaveMinidump(const debug_ipc::SaveMinidumpRequest& request,
                    fit::callback<void(const Err&, debug_ipc::SaveMinidumpReply)> cb) override;

  // DownloadObserver implementation.
  void OnDownloadsStopped(size_t num_succeeded, size_t num_failed) override;

 private:
  // Initialization routine. Iterates minidump structures and finds all the readable memory.
  // memory_ becomes valid after calling this.
  void CollectMemory();

  // Get all the modules out of the dump in debug ipc form.
  std::vector<debug_ipc::Module> GetModules();

  std::string ProcessName();

  const crashpad::ThreadSnapshot* GetThreadById(uint64_t koid);

  bool attached_ = false;
  Session* session_;

  std::unique_ptr<crashpad::ProcessSnapshotMinidump> minidump_;

  // MinidumpMemory holds the pointer to objects in minidump_. It's important to destruct or reset
  // memory_ before minidump_.
  std::unique_ptr<MinidumpMemory> memory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MinidumpRemoteAPI);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MINIDUMP_REMOTE_API_H_
