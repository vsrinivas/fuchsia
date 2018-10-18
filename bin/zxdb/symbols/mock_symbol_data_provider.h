// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/symbols/symbol_data_provider.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

// An implementation of SymbolDataProdiver for testing.
class MockSymbolDataProvider : public SymbolDataProvider {
 public:
  MockSymbolDataProvider();

  void set_ip(uint64_t ip) { ip_ = ip; }
  void set_bp(uint64_t bp) { bp_ = bp; }

  // Adds the given canned result for the given register. Set synchronous if
  // the register contents should be synchronously available, false if it
  // should require a callback to retrieve.
  void AddRegisterValue(int register_num, bool synchronous, uint64_t value);

  // Sets an expected memory value.
  void AddMemory(uint64_t address, std::vector<uint8_t> data);

  // SymbolDataProvider implementation.
  bool GetRegister(int dwarf_register_number, uint64_t* output) override;
  void GetRegisterAsync(int dwarf_register_number,
                        GetRegisterCallback callback) override;
  void GetMemoryAsync(uint64_t address, uint32_t size,
                      GetMemoryCallback callback) override;

 private:
  struct RegData {
    RegData() = default;
    RegData(bool sync, uint64_t v) : synchronous(sync), value(v) {}

    bool synchronous = false;
    uint64_t value = 0;
  };

  // Registered memory blocks indexed by address.
  using RegisteredMemory = std::map<uint64_t, std::vector<uint8_t>>;

  // Returns the memory block that contains the given address, or mem_.end()
  // if not found.
  RegisteredMemory::const_iterator FindBlockForAddress(uint64_t address) const;

  uint64_t ip_ = 0;
  uint64_t bp_ = 0;
  std::map<int, RegData> regs_;

  RegisteredMemory mem_;

  fxl::WeakPtrFactory<MockSymbolDataProvider> weak_factory_;
};

}  // namespace zxdb
