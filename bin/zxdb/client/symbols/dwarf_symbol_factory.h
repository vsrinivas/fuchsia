// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/symbol_factory.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class ModuleSymbolsImpl;

// Implementation of SymbolFactory that reads from the DWARF symbols in the
// given module.
class DwarfSymbolFactory : public SymbolFactory {
 public:
  explicit DwarfSymbolFactory(fxl::WeakPtr<ModuleSymbolsImpl> symbols);
  ~DwarfSymbolFactory() override;

  // SymbolFactory implementation.
  fxl::RefPtr<Symbol> CreateSymbol(void* data_ptr,
                                   uint32_t offset) const override;

 private:
  // This can be null if the module is unloaded but there are still some
  // dangling type references to it.
  fxl::WeakPtr<ModuleSymbolsImpl> symbols_;
};

}  // namespace zxdb
