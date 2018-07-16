// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/dwarf_symbol_factory.h"

#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"
#include "garnet/bin/zxdb/client/symbols/symbol.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"

namespace zxdb {

DwarfSymbolFactory::DwarfSymbolFactory(fxl::WeakPtr<ModuleSymbolsImpl> symbols)
    : symbols_(symbols) {}
DwarfSymbolFactory::~DwarfSymbolFactory() = default;

fxl::RefPtr<Symbol> DwarfSymbolFactory::CreateSymbol(
    void* data_ptr, uint32_t offset) const {
  if (!symbols_)
    return fxl::MakeRefCounted<Symbol>();

  auto* unit = static_cast<llvm::DWARFCompileUnit*>(data_ptr);
  llvm::DWARFDie die = unit->getDIEForOffset(offset);
  if (!die.isValid())
    return fxl::MakeRefCounted<Symbol>();

  // TODO(brettw) actually decode symbols here.

  return fxl::MakeRefCounted<Symbol>();
}

}  // namespace zxdb
