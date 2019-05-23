// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SYMBOL_DATA_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SYMBOL_DATA_PROVIDER_H_

#include <map>

#include "src/developer/debug/zxdb/common/mock_memory.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

// An implementation of SymbolDataProdiver for testing.
class MockSymbolDataProvider : public SymbolDataProvider {
 public:
  // Holds a list of time-ordered (address, data) pairs of memory.
  using MemoryWrites = std::vector<std::pair<uint64_t, std::vector<uint8_t>>>;

  MockSymbolDataProvider();

  void set_ip(uint64_t ip) { ip_ = ip; }
  void set_bp(uint64_t bp) { bp_ = bp; }

  // Adds the given canned result for the given register. Set synchronous if
  // the register contents should be synchronously available, false if it
  // should require a callback to retrieve.
  //
  // Any registers not set will be synchronously reported as unknown.
  void AddRegisterValue(debug_ipc::RegisterID id, bool synchronous,
                        uint64_t value);

  // Sets an expected memory value.
  void AddMemory(uint64_t address, std::vector<uint8_t> data);

  // Returns the list of all memory written by WriteMemory calls as a series
  // of (address, data) pairs. The stored list will be cleared by this call.
  MemoryWrites GetMemoryWrites() { return std::move(memory_writes_); }

  // SymbolDataProvider implementation.
  debug_ipc::Arch GetArch() override;
  bool GetRegister(debug_ipc::RegisterID id,
                   std::optional<uint64_t>* value) override;
  void GetRegisterAsync(debug_ipc::RegisterID id,
                        GetRegisterCallback callback) override;
  std::optional<uint64_t> GetFrameBase() override;
  void GetFrameBaseAsync(GetRegisterCallback callback) override;
  void GetMemoryAsync(uint64_t address, uint32_t size,
                      GetMemoryCallback callback) override;
  void WriteMemory(uint64_t address, std::vector<uint8_t> data,
                   std::function<void(const Err&)> cb) override;

 private:
  struct RegData {
    RegData() = default;
    RegData(bool sync, uint64_t v) : synchronous(sync), value(v) {}

    bool synchronous = false;
    uint64_t value = 0;
  };

  uint64_t ip_ = 0;
  uint64_t bp_ = 0;
  std::map<debug_ipc::RegisterID, RegData> regs_;

  MockMemory memory_;

  MemoryWrites memory_writes_;  // Logs calls to WriteMemory().

  fxl::WeakPtrFactory<MockSymbolDataProvider> weak_factory_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SYMBOL_DATA_PROVIDER_H_
