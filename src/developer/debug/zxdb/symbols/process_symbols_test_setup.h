// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_PROCESS_SYMBOLS_TEST_SETUP_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_PROCESS_SYMBOLS_TEST_SETUP_H_

#include <memory>

#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/system_symbols.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"

namespace zxdb {

class MockModuleSymbols;
class ModuleSymbols;

// This class sets up a ProcessSymbols for testing purposes. It allows MockModuleSymbols to be
// easily injected into the process.
//
// This class is only useful for tests that use the symbol system but not the client objects
// (Process/Target, etc.).
class ProcessSymbolsTestSetup {
 public:
  ProcessSymbolsTestSetup();
  ~ProcessSymbolsTestSetup();

  SystemSymbols& system() { return system_; }
  TargetSymbols& target() { return target_; }
  ProcessSymbols& process() { return process_; }

  // Appends the given module symbols implementation to the process. This will
  // typically be a MockModuleSymbols.
  void InjectModule(const std::string& name, const std::string& build_id, uint64_t base,
                    fxl::RefPtr<ModuleSymbols> mod_sym);

  // The default load address for InjectMockModule. See that for more.
  static constexpr uint64_t kDefaultLoadAddress = 0x1000000;

  // Injects a module at the address kDefaultLoadAddress, returning a pointer to it. The returned
  // pointer will be owned by the symbol system associated with the process().
  //
  // Most callers only need one module, want to use the standard mock, and don't care about the
  // particular name or load address. This uses some defaults to make injection simpler.
  //
  // This function should be called at most once.
  MockModuleSymbols* InjectMockModule();

 private:
  SystemSymbols system_;

  TargetSymbols target_;

  ProcessSymbols::Notifications process_notifications_;
  ProcessSymbols process_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_PROCESS_SYMBOLS_TEST_SETUP_H_
