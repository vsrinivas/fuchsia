// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/client/symbols/module_symbols.h"

namespace zxdb {

// A mock for symbol lookup.
class MockModuleSymbols : public ModuleSymbols {
 public:
  explicit MockModuleSymbols(const std::string& local_file_name);
  ~MockModuleSymbols() override;

  // Adds a mock mapping from the given name to the addresses.
  void AddSymbol(const std::string& name, std::vector<uint64_t> addrs);

  // ModuleSymbols implementation.
  ModuleSymbolStatus GetStatus() const override;
  Location RelativeLocationForRelativeAddress(uint64_t address) const override;
  LineDetails LineDetailsForRelativeAddress(uint64_t address) const override;
  std::vector<uint64_t> RelativeAddressesForFunction(
      const std::string& name) const override;
  std::vector<std::string> FindFileMatches(
      const std::string& name) const override;
  std::vector<uint64_t> RelativeAddressesForLine(
      const FileLine& line) const override;

 private:
  std::string local_file_name_;

  // Maps manually-added symbols to their addresses.
  std::map<std::string, std::vector<uint64_t>> symbols_;
};

}  // namespace zxdb
