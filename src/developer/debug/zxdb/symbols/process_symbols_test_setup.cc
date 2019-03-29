// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"

#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"

namespace zxdb {

ProcessSymbolsTestSetup::ProcessSymbolsTestSetup()
    : system_(),
      target_(&system_),
      process_notifications_(),
      process_(&process_notifications_, &target_) {}

ProcessSymbolsTestSetup::~ProcessSymbolsTestSetup() = default;

void ProcessSymbolsTestSetup::InjectModule(
    const std::string& name, const std::string& build_id, uint64_t base,
    std::unique_ptr<ModuleSymbols> mod_sym) {
  auto loaded = std::make_unique<LoadedModuleSymbols>(
      system_.InjectModuleForTesting(build_id, std::move(mod_sym)), build_id,
      base);
  process_.InjectModuleForTesting(name, build_id, std::move(loaded));
}

}  // namespace zxdb
