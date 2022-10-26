// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/symbol_factory.h"

#include "src/developer/debug/zxdb/symbols/lazy_symbol.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

LazySymbol SymbolFactory::MakeLazy(uint64_t die_offset) const {
  return LazySymbol(fxl::RefPtr<const SymbolFactory>(this), die_offset);
}

UncachedLazySymbol SymbolFactory::MakeUncachedLazy(uint64_t die_offset) const {
  return UncachedLazySymbol(fxl::RefPtr<const SymbolFactory>(this), die_offset);
}

}  // namespace zxdb
