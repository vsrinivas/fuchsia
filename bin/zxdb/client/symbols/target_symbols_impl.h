// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>

#include "garnet/bin/zxdb/client/symbols/system_symbols.h"
#include "garnet/bin/zxdb/client/symbols/target_symbols.h"

namespace zxdb {

class SessionSymbolsImpl;

// The current implementation is that all modifications to the list of
// symbol modules is done by the ProcessSymbolsImpl which knows the actual
// symbols of the running program. This provides the minor benefit of symbols
// being available between identical runs of the same binary (useful for
// setting breakpoints).
//
// More useful would be that the symbols could be automatically loaded when
// we know the binary we'll be running, regardless of whether it's been
// started yet. This requires some system integration (how do you find the
// local binary for something on the target?) and may also depend on how
// typical programs will be started in the debugger (which may evolve).
class TargetSymbolsImpl : public TargetSymbols {
 public:
  // This class is copyable and assignable (to support cloning Targets).
  // The SessionSymbolsImpl object must outlive this class.
  explicit TargetSymbolsImpl(SystemSymbols* system_symbols);
  TargetSymbolsImpl(const TargetSymbolsImpl& other);
  ~TargetSymbolsImpl() override;

  TargetSymbolsImpl& operator=(const TargetSymbolsImpl& other);

  SystemSymbols* system_symbols() { return system_symbols_; }

  // Notifications from ProcessSymbols to keep things in sync. Multiple add
  // notifications are allowed for the same module (this happens when
  // the symbols exist, then the process is started and the module is loaded
  // for real).
  void AddModule(fxl::RefPtr<SystemSymbols::ModuleRef> module);
  void RemoveModule(fxl::RefPtr<SystemSymbols::ModuleRef>& module);
  void RemoveAllModules();

  // TargetSymbols implementation.
  std::vector<std::string> FindFileMatches(
      const std::string& name) const override;
  std::vector<FileLine> FindLinesForSymbol(
      const std::string& name) const override;

 private:
  // Comparison functor for ModuleRefs. Does a pointer-identity comparison.
  struct ModuleRefComparePtr {
    bool operator()(const fxl::RefPtr<SystemSymbols::ModuleRef>& a,
                    const fxl::RefPtr<SystemSymbols::ModuleRef>& b) const;
  };

  SystemSymbols* const system_symbols_;  // Non-owning.

  // Since there are no addresses, there is no real ordering of these modules.
  // Track them by pointer identity to make keeping in sync with the
  // ProcessSymbols more efficient.
  std::set<fxl::RefPtr<SystemSymbols::ModuleRef>, ModuleRefComparePtr> modules_;
};

}  // namespace zxdb
