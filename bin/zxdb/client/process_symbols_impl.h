// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/client/process_symbols.h"
#include "garnet/bin/zxdb/client/system_symbols.h"
#include "garnet/public/lib/fxl/macros.h"

namespace debug_ipc {
struct Module;
}

namespace zxdb {

class ProcessImpl;

// Main client interface for querying process symbol information. See also
// TargetSymbols.
class ProcessSymbolsImpl : public ProcessSymbols {
 public:
  // The ProcessImpl must outlive this class.
  explicit ProcessSymbolsImpl(ProcessImpl* process);
  ~ProcessSymbolsImpl();

  // Adds the given module to the process. The callback will be executed with
  // the local path of the module if it is found, or the empty string if it is
  // not found.
  void AddModule(const debug_ipc::Module& module,
                 std::function<void(const std::string&)> callback);

  // Replaces all modules with the given list.
  void SetModules(const std::vector<debug_ipc::Module>& modules);

  // ProcessSymbols implementation.
  std::vector<ModuleStatus> GetStatus() const override;
  Location GetLocationForAddress(uint64_t address) const override;
  std::vector<uint64_t> GetAddressesForFunction(
      const std::string& name) const override;

 private:
  struct ModuleInfo {
    std::string name;
    std::string build_id;
    uint64_t base = 0;

    // MAY BE NULL if the symbols could not be loaded.
    fxl::RefPtr<SystemSymbols::ModuleRef> symbols;
  };

  // Looks up the given address and returns the module it's part of. Returns
  // null if the address is out-of-range.
  const ModuleInfo* InfoForAddress(uint64_t address) const;

  ProcessImpl* const process_;  // Non-owning.

  // Maps load address to the module symbol information.
  std::map<uint64_t, ModuleInfo> modules_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessSymbolsImpl);
};

}  // namespace zxdb
