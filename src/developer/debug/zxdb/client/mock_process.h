// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/process.h"

namespace zxdb {

// Provides a Process implementation that just returns empty values for
// everything. Tests can override this to implement the subset of functionality
// they need.
class MockProcess : public Process {
 public:
  explicit MockProcess(Session* session);
  ~MockProcess() override;

  // Sets the value returned by GetSymbols(). Does not take ownership.
  void set_symbols(ProcessSymbols* s) { symbols_ = s; }

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
  fxl::RefPtr<SymbolDataProvider> GetSymbolDataProvider() const override;
  void ReadMemory(
      uint64_t address, uint32_t size,
      std::function<void(const Err&, MemoryDump)> callback) override;
  void WriteMemory(uint64_t address, std::vector<uint8_t> data,
                   std::function<void(const Err&)> callback) override;

 private:
  ProcessSymbols* symbols_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockProcess);
};

}  // namespace zxdb
