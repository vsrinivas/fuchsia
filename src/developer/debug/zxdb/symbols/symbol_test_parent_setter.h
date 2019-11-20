// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_TEST_PARENT_SETTER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_TEST_PARENT_SETTER_H_

#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// Normally links to symbol parents can not hold a refcount to avoid reference cycles. This is
// why the UncachedLazySymbol exists. But some tests want to explicitly set the parent object
// without having to construct a whole mock factory.
//
// This object has access to the protected constructor of UncachedLazySymbol and is able to set
// an owning reference to a symbol on it for test purposes. To prevent leaks, this object will
// clear the parent reference when it goes out of scope.
//
// Example:
//
//   TEST(Foo, Bar) {
//     auto function = fxl::MakeRefCounted<Function>(...);
//     auto code_block = fxl::MakeRefCounted<CodeBlock>(...);
//
//     // Sets |code_block|'s parent to be |function|.
//     SymbolTestParentSetter code_block_setter(code_block, function);
//
//     ... do test ...
//   }
class SymbolTestParentSetter {
 public:
  SymbolTestParentSetter(fxl::RefPtr<Symbol> symbol, fxl::RefPtr<Symbol> parent)
      : symbol_(std::move(symbol)) {
    symbol_->set_parent(UncachedLazySymbol::MakeUnsafe(std::move(parent)));
  }

  ~SymbolTestParentSetter() { symbol_->set_parent(UncachedLazySymbol()); }

 private:
  fxl::RefPtr<Symbol> symbol_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_TEST_PARENT_SETTER_H_
