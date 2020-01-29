// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"

#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"

namespace zxdb {

LoadedModuleSymbols::LoadedModuleSymbols(fxl::RefPtr<ModuleSymbols> module, std::string build_id,
                                         uint64_t load_address, uint64_t debug_address)
    : module_(std::move(module)),
      load_address_(load_address),
      debug_address_(debug_address),
      build_id_(std::move(build_id)),
      symbol_context_(load_address),
      weak_factory_(this) {}

LoadedModuleSymbols::~LoadedModuleSymbols() = default;

fxl::WeakPtr<LoadedModuleSymbols> LoadedModuleSymbols::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

std::vector<Location> LoadedModuleSymbols::ResolveInputLocation(
    const InputLocation& input_location, const ResolveOptions& options) const {
  if (module_) {
    return module_symbols()->ResolveInputLocation(symbol_context(), input_location, options);
  }

  return std::vector<Location>();
}

}  // namespace zxdb
