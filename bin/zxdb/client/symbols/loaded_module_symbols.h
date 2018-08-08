// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/symbols/system_symbols.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class FileLine;
class LineDetails;

// Represents the symbol information for a module that's loaded. This just
// references the underlying ModuleSymbols (which is the same regardless of
// load address) and holds the load address.
class LoadedModuleSymbols {
 public:
  LoadedModuleSymbols(fxl::RefPtr<SystemSymbols::ModuleRef> module,
                      uint64_t load_address);
  ~LoadedModuleSymbols();

  // Returns the underlying ModuleSymbols object.
  SystemSymbols::ModuleRef* module_ref() { return module_.get(); }
  const ModuleSymbols* module_symbols() const {
    return module_->module_symbols();
  }

  // Base address for the module.
  uint64_t load_address() const { return load_address_; }

  // Most functions in ModuleSymbols take a symbol context to convery between
  // absolute addresses in memory to ones relative to the module load address.
  const SymbolContext& symbol_context() const { return symbol_context_; }

 private:
  fxl::RefPtr<SystemSymbols::ModuleRef> module_;

  uint64_t load_address_;
  SymbolContext symbol_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LoadedModuleSymbols);
};

}  // namespace zxdb
