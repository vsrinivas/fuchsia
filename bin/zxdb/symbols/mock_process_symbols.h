// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <vector>

#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/process_symbols.h"

namespace zxdb {

// This is useful for testing things that only use a ProcessSymbols and have
// minimal requirements. More elaborate tests can use the real ProcessSymbols
// implementation with MockModuleSymbols backing it. See
// ProcessSymbolsImplTestSetup.
class MockProcessSymbols : public ProcessSymbols {
 public:
  MockProcessSymbols();
  ~MockProcessSymbols() override;

  // Sets a response for a symbol query of the given symbol.
  void AddSymbol(const std::string& name, std::vector<Location> locations);

  // ProcessSymbols implementation.
  fxl::WeakPtr<const ProcessSymbols> GetWeakPtr() const override;
  TargetSymbols* GetTargetSymbols() override;
  std::vector<ModuleSymbolStatus> GetStatus() const override;
  std::vector<const LoadedModuleSymbols*> GetLoadedModuleSymbols()
      const override;
  std::vector<Location> ResolveInputLocation(
      const InputLocation& input_location,
      const ResolveOptions& options) const override;
  LineDetails LineDetailsForAddress(uint64_t address) const override;
  bool HaveSymbolsLoadedForModuleAt(uint64_t address) const override;

 private:
  std::map<std::string, std::vector<Location>> symbols_;

  mutable fxl::WeakPtrFactory<const ProcessSymbols> weak_factory_;
};

}  // namespace zxdb
