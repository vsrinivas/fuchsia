// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/zxdb/symbols/process_symbols_impl.h"
#include "garnet/bin/zxdb/symbols/system_symbols.h"
#include "garnet/bin/zxdb/symbols/target_symbols_impl.h"

namespace zxdb {

class ModuleSymbols;

// This class sets up a ProcessSymbolsImpl for testing purposes. It allows
// MockModuleSymbols to be easily injected into the process.
//
// This class is only useful for tests that use the symbol system but not the
// client objects (Process/Target, etc.).
class ProcessSymbolsImplTestSetup {
 public:
  ProcessSymbolsImplTestSetup();
  ~ProcessSymbolsImplTestSetup();

  SystemSymbols& system() { return system_; }
  TargetSymbolsImpl& target() { return target_; }
  ProcessSymbolsImpl& process() { return process_; }

  // Appends the given module symbols implementation to the process. This will
  // typically be a MockModuleSymbols.
  void InjectModule(const std::string& name, const std::string& build_id,
                    uint64_t base, std::unique_ptr<ModuleSymbols> mod_sym);

 private:
  SystemSymbols system_;

  TargetSymbolsImpl target_;

  ProcessSymbolsImpl::Notifications process_notifications_;
  ProcessSymbolsImpl process_;
};

}  // namespace zxdb
