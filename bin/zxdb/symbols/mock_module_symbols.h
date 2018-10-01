// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/symbols/module_symbols.h"

namespace zxdb {

// A mock for symbol lookup.
class MockModuleSymbols : public ModuleSymbols {
 public:
  explicit MockModuleSymbols(const std::string& local_file_name);
  ~MockModuleSymbols() override;

  // Adds a mock mapping from the given name to the addresses.
  void AddSymbol(const std::string& name, std::vector<uint64_t> addrs);

  // Adds a mock mapping from address to line details. This matches an exact
  // address only, not a range.
  void AddLineDetails(uint64_t absolute_address, LineDetails details);

  // ModuleSymbols implementation.
  ModuleSymbolStatus GetStatus() const override;
  Location LocationForAddress(const SymbolContext& symbol_context,
                              uint64_t address) const override;
  std::vector<Location> ResolveInputLocation(
      const SymbolContext& symbol_context, const InputLocation& input_location,
      const ResolveOptions& options) const override;
  LineDetails LineDetailsForAddress(const SymbolContext& symbol_context,
                                    uint64_t address) const override;
  std::vector<uint64_t> AddressesForFunction(
      const SymbolContext& symbol_context,
      const std::string& name) const override;
  std::vector<std::string> FindFileMatches(
      const std::string& name) const override;
  std::vector<uint64_t> AddressesForLine(const SymbolContext& symbol_context,
                                         const FileLine& line) const override;

 private:
  std::string local_file_name_;

  // Maps manually-added symbols to their addresses.
  std::map<std::string, std::vector<uint64_t>> symbols_;

  // Maps manually-added addresses to line details.
  std::map<uint64_t, LineDetails> lines_;
};

}  // namespace zxdb
