// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/loaded_module_symbols.h"
#include "garnet/bin/zxdb/client/symbols/system_symbols.h"

namespace zxdb {

class LoadedModuleSymbolsImpl : public LoadedModuleSymbols {
 public:
  LoadedModuleSymbolsImpl(fxl::RefPtr<SystemSymbols::ModuleRef> module,
                          uint64_t load_address);
  ~LoadedModuleSymbolsImpl() override;

  fxl::RefPtr<SystemSymbols::ModuleRef>& module() { return module_; }

  // LoadedModuleSymbols implementation.
  ModuleSymbols* GetModuleSymbols() override;
  uint64_t GetLoadAddress() const override;
  Location LocationForAddress(uint64_t address) const override;
  LineDetails LineDetailsForAddress(uint64_t address) const override;
  std::vector<uint64_t> AddressesForFunction(
      const std::string& name) const override;
  std::vector<uint64_t> AddressesForLine(const FileLine& line) const override;

 private:
  fxl::RefPtr<SystemSymbols::ModuleRef> module_;
  uint64_t load_address_;

  // Converts between relative and absolute addresses.
  uint64_t RelativeToAbsolute(uint64_t relative_address) const;
  uint64_t AbsoluteToRelative(uint64_t absolute_address) const;

  FXL_DISALLOW_COPY_AND_ASSIGN(LoadedModuleSymbolsImpl);
};

}  // namespace zxdb
