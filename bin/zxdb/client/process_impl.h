// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/process.h"

#include <map>
#include <memory>

#include "garnet/bin/zxdb/client/symbols/process_symbols_impl.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class TargetImpl;
class ThreadImpl;

class ProcessImpl : public Process, public ProcessSymbolsImpl::Notifications {
 public:
  ProcessImpl(TargetImpl* target, uint64_t koid, const std::string& name);
  ~ProcessImpl() override;

  ThreadImpl* GetThreadImplFromKoid(uint64_t koid);

  TargetImpl* target() const { return target_; }

  // Process implementation:
  Target* GetTarget() const override;
  uint64_t GetKoid() const override;
  const std::string& GetName() const override;
  ProcessSymbols* GetSymbols() override;
  void GetModules(
      std::function<void(const Err&, std::vector<debug_ipc::Module>)>) override;
  void GetAspace(
      uint64_t address,
      std::function<void(const Err&, std::vector<debug_ipc::AddressRegion>)>)
      const override;
  std::vector<Thread*> GetThreads() const override;
  Thread* GetThreadFromKoid(uint64_t koid) override;
  void SyncThreads(std::function<void()> callback) override;
  void Pause() override;
  void Continue() override;
  void ContinueUntil(const InputLocation& location,
                     std::function<void(const Err&)> cb) override;
  void ReadMemory(
      uint64_t address, uint32_t size,
      std::function<void(const Err&, MemoryDump)> callback) override;

  // Notifications from the agent that a thread has started or exited.
  void OnThreadStarting(const debug_ipc::ThreadRecord& record);
  void OnThreadExiting(const debug_ipc::ThreadRecord& record);

  // Notification that the list of loaded modules may have been updated.
  void OnModules(const std::vector<debug_ipc::Module>& modules);

 private:
  // Syncs the threads_ list to the new list of threads passed in .
  void UpdateThreads(const std::vector<debug_ipc::ThreadRecord>& new_threads);

  // ProcessSymbolsImpl::Notifications implementation:
  void DidLoadModuleSymbols(LoadedModuleSymbols* module) override;
  void WillUnloadModuleSymbols(LoadedModuleSymbols* module) override;
  void OnSymbolLoadFailure(const Err& err) override;

  TargetImpl* const target_;  // The target owns |this|.
  const uint64_t koid_;
  std::string name_;

  // Threads indexed by their thread koid.
  std::map<uint64_t, std::unique_ptr<ThreadImpl>> threads_;

  ProcessSymbolsImpl symbols_;

  fxl::WeakPtrFactory<ProcessImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessImpl);
};

}  // namespace zxdb
