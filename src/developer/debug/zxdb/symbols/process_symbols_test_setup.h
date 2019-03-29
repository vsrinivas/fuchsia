// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/system_symbols.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"

namespace zxdb {

class ModuleSymbols;

// This class sets up a ProcessSymbols for testing purposes. It allows
// MockModuleSymbols to be easily injected into the process.
//
// This class is only useful for tests that use the symbol system but not the
// client objects (Process/Target, etc.).
class ProcessSymbolsTestSetup {
 public:
  ProcessSymbolsTestSetup();
  ~ProcessSymbolsTestSetup();

  SystemSymbols& system() { return system_; }
  TargetSymbols& target() { return target_; }
  ProcessSymbols& process() { return process_; }

  // Appends the given module symbols implementation to the process. This will
  // typically be a MockModuleSymbols.
  void InjectModule(const std::string& name, const std::string& build_id,
                    uint64_t base, std::unique_ptr<ModuleSymbols> mod_sym);

 private:
  SystemSymbols system_;

  TargetSymbols target_;

  ProcessSymbols::Notifications process_notifications_;
  ProcessSymbols process_;
};

}  // namespace zxdb
