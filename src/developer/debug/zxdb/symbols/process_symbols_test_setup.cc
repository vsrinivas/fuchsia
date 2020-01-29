// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"

#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"

namespace zxdb {

ProcessSymbolsTestSetup::ProcessSymbolsTestSetup()
    : system_(nullptr),
      target_(&system_),
      process_notifications_(),
      process_(&process_notifications_, &target_) {}

ProcessSymbolsTestSetup::~ProcessSymbolsTestSetup() = default;

void ProcessSymbolsTestSetup::InjectModule(const std::string& name, const std::string& build_id,
                                           uint64_t base, fxl::RefPtr<ModuleSymbols> mod_sym) {
  system_.InjectModuleForTesting(build_id, mod_sym.get());
  process_.InjectModuleForTesting(
      name, build_id, std::make_unique<LoadedModuleSymbols>(std::move(mod_sym), build_id, base, 0));
}

MockModuleSymbols* ProcessSymbolsTestSetup::InjectMockModule() {
  const char kModuleName[] = "default_mock_module.so";
  const char kDefaultBuildID[] = "default_build_id";

  auto module_symbols = fxl::MakeRefCounted<MockModuleSymbols>(kModuleName);
  MockModuleSymbols* module_symbols_ptr = module_symbols.get();  // Save raw ptr to return.
  InjectModule(kModuleName, kDefaultBuildID, kDefaultLoadAddress, std::move(module_symbols));
  return module_symbols_ptr;
}

}  // namespace zxdb
