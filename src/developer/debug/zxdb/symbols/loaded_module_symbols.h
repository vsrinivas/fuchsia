// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LOADED_MODULE_SYMBOLS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LOADED_MODULE_SYMBOLS_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"
#include "src/developer/debug/zxdb/symbols/system_symbols.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class FileLine;
class LineDetails;

// Represents the symbol information for a module that's loaded. This just references the underlying
// ModuleSymbols (which is the same regardless of load address) and holds the load address.
class LoadedModuleSymbols {
 public:
  LoadedModuleSymbols(fxl::RefPtr<ModuleSymbols> module, std::string build_id,
                      uint64_t load_address, uint64_t debug_address);
  ~LoadedModuleSymbols();

  // Returns the underlying ModuleSymbols object.
  // MAY BE NULL if the symbols could not be loaded.
  const ModuleSymbols* module_symbols() const { return module_.get(); }
  ModuleSymbols* module_symbols() { return module_.get(); }
  fxl::RefPtr<ModuleSymbols> module_symbols_ref() const { return module_; }

  fxl::WeakPtr<LoadedModuleSymbols> GetWeakPtr();

  // Base address for the module.
  uint64_t load_address() const { return load_address_; }

  // Address of the link map for the module.
  uint64_t debug_address() const { return debug_address_; }

  // Build ID for the module.
  const std::string& build_id() const { return build_id_; }

  // Most functions in ModuleSymbols take a symbol context to convert between absolute addresses in
  // memory to ones relative to the module load address.
  const SymbolContext& symbol_context() const { return symbol_context_; }

  // Converts the given InputLocation into one or more locations. If the location is an address, it
  // will be be returned whether or not the address is inside this module (it will be symbolized if
  // possible). If the input is a function or file/line, they could match more than one location and
  // all locations will be returned.
  //
  // If symbolize is true, the results will be symbolized, otherwise the output locations will be
  // regular addresses (this will be slightly faster).
  //
  // LINE LOOKUP
  // -----------
  // Finds the addresses for all instantiations of the given line. Often there will be one result,
  // but inlining and templates could duplicate the code.
  //
  // It may not be possible to return the exact line. The line could have been optimized out, it
  // could have been a continuation of an earlier line, or there could be no code at that line in
  // the first place. This function will try its best to find the best line if an exact match isn't
  // possible.
  //
  // This function will also match multiple different input files according to FindFileMatches()
  // rules. If you don't want this, we can add a field to ResolveOptions to disable this and force
  // exact matches.
  //
  // If you need to find out the exact actual location that this resolved to, look up the resulting
  // address again.
  //
  // If the file wasn't found or contains no code, it will return an empty vector. If the file
  // exists and contains code, it will always return *something*.
  //
  // SYMBOL LOOKUP
  // -------------
  // Returns the addresses for the given function name. The function name must be an exact match.
  // The addresses will indicate the start of the function. Since a function implementation can be
  // duplicated more than once, there can be multiple results.
  std::vector<Location> ResolveInputLocation(
      const InputLocation& input_location, const ResolveOptions& options = ResolveOptions()) const;

 private:
  fxl::RefPtr<ModuleSymbols> module_;

  uint64_t load_address_;
  uint64_t debug_address_;
  std::string build_id_;
  SymbolContext symbol_context_;

  fxl::WeakPtrFactory<LoadedModuleSymbols> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LoadedModuleSymbols);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LOADED_MODULE_SYMBOLS_H_
