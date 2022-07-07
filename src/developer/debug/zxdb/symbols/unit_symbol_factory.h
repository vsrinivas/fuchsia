// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_UNIT_SYMBOL_FACTORY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_UNIT_SYMBOL_FACTORY_H_

#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// A factory for creating symbol objects because on unit-relative offsets. Some DWARF constructs
// refer to DIE entries via unit-relative offsets. Code needing this capability can be passed this
// object.
//
// This object is extremely simple and just encodes a Symbolfactory + unit_offset. It can be used
// for mock symbols in tests by using a MockSymbolFactory and arranging for mock symbols to be
// generated at the "unit_loc + offset" locations. The unit offset is never actually dereferenced so
// a DWARF unit does not need to actually be present at the unit_loc for testing.
//
// Copying and passing-by-value is OK.
class UnitSymbolFactory {
 public:
  // A default-constructred UnitSymbolFactory returns empty LazySymbols.
  UnitSymbolFactory() = default;

  UnitSymbolFactory(fxl::RefPtr<const SymbolFactory> factory, uint64_t unit_loc)
      : symbol_factory_(std::move(factory)), unit_loc_(unit_loc) {}

  // Constructs a UnitSymbolFactory for the unit containing the given Symbol. On failure this
  // generates a symbol factory that returns empty LazySymbols.
  explicit UnitSymbolFactory(const Symbol* symbol) {
    fxl::WeakPtr<ModuleSymbols> weak_mod = symbol->GetModuleSymbols();
    if (!weak_mod)
      return;

    fxl::RefPtr<CompileUnit> unit = symbol->GetCompileUnit();
    if (!unit)
      return;

    symbol_factory_ = fxl::RefPtr<const SymbolFactory>(weak_mod->GetSymbolFactory());
    unit_loc_ = unit->die_addr();
  }

  // Constructs a lazy symbol given a unit-relative DIE offset.
  //
  // We could also add an "Uncached" variant in the future if needed.
  LazySymbol MakeLazyUnitRelative(uint64_t offset_from_unit) const {
    if (symbol_factory_ && unit_loc_)
      return LazySymbol(symbol_factory_, *unit_loc_ + offset_from_unit);
    return LazySymbol();
  }

 private:
  fxl::RefPtr<const SymbolFactory> symbol_factory_;
  std::optional<uint64_t> unit_loc_;  // Nullopt on uninitialized or error.
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_UNIT_SYMBOL_FACTORY_H_
