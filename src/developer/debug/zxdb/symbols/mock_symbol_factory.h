// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SYMBOL_FACTORY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SYMBOL_FACTORY_H_

#include <map>

#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/symbols/lazy_symbol.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/developer/debug/zxdb/symbols/symbol_factory.h"

namespace zxdb {

// Symbols have a backpointer to their SymbolFactory, yet the mock symbol factory must contain
// owning references to the symbols it vends. This creates a reference cycle that will leak.
//
// To get around this, the MockSymbolFactory is not actually a SymbolFactory implementat, but a
// non-reference-counted wrapper object you should create on the stack (or as a member of your test
// harness). It will clear all of the symbol references in the actual SymbolFactory implementation
// when it goes out of scope, breaking the reference cycle.
class MockSymbolFactory {
 public:
  MockSymbolFactory() = default;
  ~MockSymbolFactory() {
    // Break the reference cycles.
    factory_->ClearSymbols();
  }

  fxl::RefPtr<SymbolFactory> factory_ref() const { return factory_; }
  const SymbolFactory* factory() const { return factory_.get(); }

  void SetMockSymbol(uint64_t die_offset, fxl::RefPtr<Symbol> symbol) {
    factory_->SetMockSymbol(die_offset, symbol);
  }

 private:
  class FactoryImpl : public SymbolFactory {
   public:
    // SymbolFactory implementation:
    fxl::RefPtr<Symbol> CreateSymbol(uint64_t die_offset) const override {
      if (auto found = symbols_.find(die_offset); found != symbols_.end())
        return found->second;

      // Never return null on failure, error is indicated by a default-constructed Symbol.
      return fxl::MakeRefCounted<Symbol>();
    }

    // Adds a mock symbol to the factory that will be returned when queried for the given offset.
    //
    // This also updates the symbol's UncachedLazySymbol to point to this factory so round-trip
    // queries will work. This creates a reference cycle as mentioned at the top of the file.
    void SetMockSymbol(uint64_t die_offset, fxl::RefPtr<Symbol> symbol) {
      symbol->set_lazy_this(UncachedLazySymbol(RefPtrTo(this), die_offset));
      symbols_[die_offset] = std::move(symbol);
    }

    // Releases all references to mock symbols.
    void ClearSymbols() { symbols_.clear(); }

   private:
    std::map<uint64_t, fxl::RefPtr<Symbol>> symbols_;
  };

  fxl::RefPtr<FactoryImpl> factory_ = fxl::MakeRefCounted<FactoryImpl>();
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SYMBOL_FACTORY_H_
