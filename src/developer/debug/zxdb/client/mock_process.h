// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_PROCESS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_PROCESS_H_

#include "src/developer/debug/zxdb/client/process.h"

namespace zxdb {

// Provides a Process implementation that just returns empty values for everything. Tests can
// override this to implement the subset of functionality they need.
class MockProcess : public Process {
 public:
  explicit MockProcess(Session* session);
  ~MockProcess() override;

  // Sets the value returned by GetSymbols(). Does not take ownership.
  void set_symbols(ProcessSymbols* s) { symbols_ = s; }

  // Sets the value returned by GetTLSHelpers().
  void set_tls_helpers(TLSHelpers h) { tls_helpers_ = h; }

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

 private:
  ProcessSymbols* symbols_ = nullptr;
  std::optional<TLSHelpers> tls_helpers_ = std::nullopt;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockProcess);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_PROCESS_H_
