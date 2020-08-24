// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_IMPL_H_

#include <map>
#include <memory>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class BacktraceCache;
class ProcessSymbolDataProvider;
class TargetImpl;
class ThreadImpl;

class ProcessImpl : public Process, public ProcessSymbols::Notifications {
 public:
  ProcessImpl(TargetImpl* target, uint64_t koid, const std::string& name,
              Process::StartType start_type);
  ~ProcessImpl() override;

  ThreadImpl* GetThreadImplFromKoid(uint64_t koid);

  TargetImpl* target() const { return target_; }

  // Process implementation:
  Target* GetTarget() const override;
  uint64_t GetKoid() const override;
  const std::string& GetName() const override;
  ProcessSymbols* GetSymbols() override;
  void GetModules(fit::callback<void(const Err&, std::vector<debug_ipc::Module>)>) override;
  void GetAspace(
      uint64_t address,
      fit::callback<void(const Err&, std::vector<debug_ipc::AddressRegion>)>) const override;
  std::vector<Thread*> GetThreads() const override;
  Thread* GetThreadFromKoid(uint64_t koid) override;
  void SyncThreads(fit::callback<void()> callback) override;
  void Pause(fit::callback<void()> on_paused) override;
  void Continue(bool forward_exceptions) override;
  void ContinueUntil(std::vector<InputLocation> location,
                     fit::callback<void(const Err&)> cb) override;
  fxl::RefPtr<SymbolDataProvider> GetSymbolDataProvider() const override;
  void GetTLSHelpers(GetTLSHelpersCallback cb) override;
  void ReadMemory(uint64_t address, uint32_t size,
                  fit::callback<void(const Err&, MemoryDump)> callback) override;
  void WriteMemory(uint64_t address, std::vector<uint8_t> data,
                   fit::callback<void(const Err&)> callback) override;
  void LoadInfoHandleTable(
      fit::callback<void(ErrOr<std::vector<debug_ipc::InfoHandleExtended>> handles)> callback)
      override;

  // Notifications from the agent that a thread has started or exited.
  void OnThreadStarting(const debug_ipc::ThreadRecord& record, bool resume);
  void OnThreadExiting(const debug_ipc::ThreadRecord& record);

  // Notification that the list of loaded modules may have been updated.
  void OnModules(std::vector<debug_ipc::Module> modules,
                 const std::vector<uint64_t>& stopped_thread_koids);

  // Returns true if the caller should show the output. False means silence.
  bool HandleIO(const debug_ipc::NotifyIO&);

  // ProcessSymbols::Notifications implementation (public portion):
  void OnSymbolLoadFailure(const Err& err) override;

 private:
  enum {
    kUnloaded,
    kLoading,
    kLoaded,
    kFailed,
  } tls_helper_state_ = kUnloaded;

  // Syncs the threads_ list to the new list of threads passed in .
  void UpdateThreads(const std::vector<debug_ipc::ThreadRecord>& new_threads);

  // ProcessSymbols::Notifications implementation:
  void DidLoadModuleSymbols(LoadedModuleSymbols* module) override;
  void WillUnloadModuleSymbols(LoadedModuleSymbols* module) override;

  uint64_t GetSymbolAddress(const std::string& symbol, uint64_t* size);

  // Run the given callback as soon as the TLS helpers are loaded. If the TLS helpers failed to
  // load, pass false to the callback.
  void DoWithHelpers(fit::callback<void(bool)> cb);

  // Load the TLS helpers.
  void LoadTLSHelpers();

  // Updates modules with empty names to reflect the name of the process binary. By convention,
  // the dynamic loader will set the main binary to have a blank name.
  void FixupEmptyModuleNames(std::vector<debug_ipc::Module>& modules) const;

  TargetImpl* const target_;  // The target owns |this|.
  const uint64_t koid_;
  std::string name_;

  // Threads indexed by their thread koid.
  std::map<uint64_t, std::unique_ptr<ThreadImpl>> threads_;

  ProcessSymbols symbols_;

  // TLS Helper blobs.
  TLSHelpers tls_helpers_;

  // Queue of tasks waiting for the helper blobs to be loaded.
  std::vector<fit::callback<void(bool)>> helper_waiters_;

  // Lazily-populated.
  mutable fxl::RefPtr<ProcessSymbolDataProvider> symbol_data_provider_;

  fxl::WeakPtrFactory<ProcessImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_IMPL_H_
