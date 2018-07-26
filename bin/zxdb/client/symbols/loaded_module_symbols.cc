// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/loaded_module_symbols.h"

namespace zxdb {

LoadedModuleSymbols::LoadedModuleSymbols(
    fxl::RefPtr<SystemSymbols::ModuleRef> module,
    uint64_t load_address)
    : module_(std::move(module)),
      load_address_(load_address),
      symbol_context_(load_address) {}

LoadedModuleSymbols::~LoadedModuleSymbols() = default;

}  // namespace zxdb
