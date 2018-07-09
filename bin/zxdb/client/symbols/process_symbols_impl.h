// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <map>
#include <memory>

#include "garnet/bin/zxdb/client/symbols/process_symbols.h"
#include "garnet/bin/zxdb/client/symbols/system_symbols.h"
#include "garnet/public/lib/fxl/macros.h"

namespace debug_ipc {
struct Module;
}

namespace zxdb {

class LoadedModuleSymbols;
class LoadedModuleSymbolsImpl;
class TargetSymbolsImpl;

// Main client interface for querying process symbol information. See also
// TargetSymbols.
class ProcessSymbolsImpl : public ProcessSymbols {
 public:
  // A simple observer interface. This allows ProcessImpl to expose these
  // in the ProcessObserver observer API. If the API here gets too much more
  // complicated, it could be we want a separate public ProcessSymbolsObserver
  // class that consumers need to register for explicitly.
  //
  // See the corresponding functions in ProcessObserver for docs.
  class Notifications {
   public:
    virtual void DidLoadModuleSymbols(LoadedModuleSymbols* module) = 0;
    virtual void WillUnloadModuleSymbols(LoadedModuleSymbols* module) = 0;
    virtual void OnSymbolLoadFailure(const Err& err) = 0;
  };

  // The passed-in pointers must outlive this class.
  ProcessSymbolsImpl(Notifications* notifications,
                     TargetSymbolsImpl* target_symbols);
  ~ProcessSymbolsImpl();

  TargetSymbolsImpl* target_symbols() { return target_symbols_; }

  // Replaces all modules with the given list.
  void SetModules(const std::vector<debug_ipc::Module>& modules);

  // ProcessSymbols implementation.
  TargetSymbols* GetTargetSymbols() override;
  std::vector<ModuleStatus> GetStatus() const override;
  Location LocationForAddress(uint64_t address) const override;
  LineDetails LineDetailsForAddress(uint64_t address) const override;
  std::vector<uint64_t> AddressesForFunction(
      const std::string& name) const override;
  std::vector<uint64_t> AddressesForLine(const FileLine& line) const override;

 private:
  struct ModuleInfo {
    std::string name;
    std::string build_id;
    uint64_t base = 0;

    // MAY BE NULL if the symbols could not be loaded.
    std::unique_ptr<LoadedModuleSymbolsImpl> symbols;
  };

  // Creates the ModuleInfo structure, attempts to load the symbols, and
  // updates the modules_ list for this process. *err will be filled with the
  // success code of symbol loading (the function will save the ModuleInfo
  // either way).
  //
  // This class issues no notifications, the caller needs to do that. Just
  // because there's no error doesn't necessarily mean the symbols have been
  // loaded, since some symbols might be expected to be not present.
  ModuleInfo* SaveModuleInfo(const debug_ipc::Module& module,
                             Err* symbol_load_err);

  // Equality comparison for the two types of modules. This compares load
  // address and build id.
  static bool RefersToSameModule(const debug_ipc::Module& a,
                                 const ModuleInfo& b);

  // Looks up the given address and returns the module it's part of. Returns
  // null if the address is out-of-range.
  const ModuleInfo* InfoForAddress(uint64_t address) const;

  Notifications* const notifications_;       // Non-owning.
  TargetSymbolsImpl* const target_symbols_;  // Non-owning.

  // Maps load address to the module symbol information.
  using ModuleMap = std::map<uint64_t, ModuleInfo>;
  ModuleMap modules_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessSymbolsImpl);
};

}  // namespace zxdb
