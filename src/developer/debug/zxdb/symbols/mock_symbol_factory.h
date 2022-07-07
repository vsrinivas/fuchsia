// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SYMBOL_FACTORY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SYMBOL_FACTORY_H_

#include <map>

#include "src/developer/debug/zxdb/symbols/symbol_factory.h"

namespace zxdb {

class MockSymbolFactory : public SymbolFactory {
 public:
  // SymbolFactory implementation:
  fxl::RefPtr<Symbol> CreateSymbol(uint64_t factory_data) const override {
    if (auto found = symbols_.find(factory_data); found != symbols_.end())
      return found->second;

    // Never return null on failure, error is indicated by a default-constructed Symbol.
    return fxl::MakeRefCounted<Symbol>();
  }

  // Adds a mock symbol to the factory that will be returned when queried for the given identifier.
  void SetMockSymbol(uint64_t identifier, fxl::RefPtr<Symbol> symbol) {
    symbols_[identifier] = std::move(symbol);
  }

 private:
  std::map<uint64_t, fxl::RefPtr<Symbol>> symbols_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SYMBOL_FACTORY_H_
