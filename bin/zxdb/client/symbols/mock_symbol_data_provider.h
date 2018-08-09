// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

// An implementation of SymbolDataProdiver for testing.
class MockSymbolDataProvider : public SymbolDataProvider {
 public:
  MockSymbolDataProvider();

  // Adds the given canned result for the given register. Set synchronous if
  // the register contents should be synchronously available, false if it
  // should require a callback to retrieve.
  void AddRegisterValue(int register_num, bool synchronous, uint64_t value);

  // SymbolDataProvider implementation.
  bool GetRegister(int dwarf_register_number, uint64_t* output) override;
  void GetRegisterAsync(
      int dwarf_register_number,
      std::function<void(bool success, uint64_t value)> callback) override;
  void GetMemoryAsync(
      uint64_t address, uint32_t size,
      std::function<void(const uint8_t* data)> callback) override;

 private:
  struct RegData {
    RegData() = default;
    RegData(bool sync, uint64_t v) : synchronous(sync), value(v) {}

    bool synchronous = false;
    uint64_t value = 0;
  };

  std::map<int, RegData> regs_;

  fxl::WeakPtrFactory<MockSymbolDataProvider> weak_factory_;
};

}  // namespace zxdb
