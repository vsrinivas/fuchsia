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
  // An insertion-time ordered list of (register, data) pairs of writes.
  using RegisterWrites = std::vector<std::pair<debug_ipc::RegisterID, std::vector<uint8_t>>>;

  // Holds a list of time-ordered (address, data) pairs of memory writes.
  using MemoryWrites = std::vector<std::pair<uint64_t, std::vector<uint8_t>>>;

  MockSymbolDataProvider();

  void set_ip(uint64_t ip) { ip_ = ip; }
  void set_bp(uint64_t bp) { bp_ = bp; }
  void set_cfa(uint64_t cfa) { cfa_ = cfa; }
  void set_tls_segment(uint64_t address) { tls_segment_ = address; }

  // Adds the given canned result for the given register. Set synchronous if the register contents
  // should be synchronously available, false if it should require a callback to retrieve. If the
  // uint64_t version is called, the register is assumed to be 64 bits.
  //
  // Any registers not set will be synchronously reported as unknown.
  void AddRegisterValue(debug_ipc::RegisterID id, bool synchronous, uint64_t value);
  void AddRegisterValue(debug_ipc::RegisterID id, bool synchronous, std::vector<uint8_t> value);

  // Sets an expected memory value.
  void AddMemory(uint64_t address, std::vector<uint8_t> data);

  // Returns the list of all memory written by WriteRegister/WriteMemory calls as a series of (dest,
  // data) pairs. The stored list will be cleared by this call.
  RegisterWrites GetRegisterWrites() { return std::move(register_writes_); }
  MemoryWrites GetMemoryWrites() { return std::move(memory_writes_); }

  // SymbolDataProvider implementation.
  debug_ipc::Arch GetArch() override;
  std::optional<containers::array_view<uint8_t>> GetRegister(debug_ipc::RegisterID id) override;
  void GetRegisterAsync(debug_ipc::RegisterID id, GetRegisterCallback callback) override;
  void WriteRegister(debug_ipc::RegisterID id, std::vector<uint8_t> data,
                     WriteCallback cb) override;
  std::optional<uint64_t> GetFrameBase() override;
  void GetFrameBaseAsync(GetFrameBaseCallback callback) override;
  std::optional<uint64_t> GetDebugAddressForContext(const SymbolContext&) const override;
  void GetTLSSegment(const SymbolContext& symbol_context, GetTLSSegmentCallback cb) override;
  uint64_t GetCanonicalFrameAddress() const override;
  void GetMemoryAsync(uint64_t address, uint32_t size, GetMemoryCallback callback) override;
  void WriteMemory(uint64_t address, std::vector<uint8_t> data, WriteCallback cb) override;

 private:
  struct RegData {
    RegData() = default;
    RegData(bool sync, std::vector<uint8_t> v) : synchronous(sync), value(v) {}

    bool synchronous = false;
    std::vector<uint8_t> value;
  };

  uint64_t ip_ = 0;
  uint64_t bp_ = 0;
  uint64_t cfa_ = 0;
  uint64_t tls_segment_;
  std::optional<std::tuple<uint8_t*, uint8_t*, uint8_t*>> tls_helpers_;
  std::map<debug_ipc::RegisterID, RegData> regs_;

  MockMemory memory_;

  RegisterWrites register_writes_;  // Logs calls to WriteRegister().
  MemoryWrites memory_writes_;      // Logs calls to WriteMemory().

  fxl::WeakPtrFactory<MockSymbolDataProvider> weak_factory_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SYMBOL_DATA_PROVIDER_H_
