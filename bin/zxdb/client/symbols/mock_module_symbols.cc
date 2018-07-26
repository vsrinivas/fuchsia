// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/mock_module_symbols.h"

#include "garnet/bin/zxdb/client/symbols/line_details.h"
#include "garnet/bin/zxdb/client/symbols/location.h"

namespace zxdb {

MockModuleSymbols::MockModuleSymbols(const std::string& local_file_name)
    : local_file_name_(local_file_name) {}
MockModuleSymbols::~MockModuleSymbols() = default;

void MockModuleSymbols::AddSymbol(const std::string& name,
                                  std::vector<uint64_t> addrs) {
  symbols_[name] = std::move(addrs);
}

ModuleSymbolStatus MockModuleSymbols::GetStatus() const {
  ModuleSymbolStatus status;
  status.name = local_file_name_;
  status.functions_indexed = symbols_.size();
  status.symbols_loaded = true;
  return status;
}

Location MockModuleSymbols::LocationForAddress(
    const SymbolContext& symbol_context, uint64_t absolute_address) const {
  // Currently only name -> address mappings are supported by this mock.
  return Location(Location::State::kAddress, absolute_address);
}

LineDetails MockModuleSymbols::LineDetailsForAddress(
    const SymbolContext& symbol_context, uint64_t address) const {
  return LineDetails();
}

std::vector<uint64_t> MockModuleSymbols::AddressesForFunction(
    const SymbolContext& symbol_context, const std::string& name) const {
  auto found = symbols_.find(name);
  if (found == symbols_.end())
    return std::vector<uint64_t>();
  return found->second;
}

std::vector<std::string> MockModuleSymbols::FindFileMatches(
    const std::string& name) const {
  return std::vector<std::string>();
}

std::vector<uint64_t> MockModuleSymbols::AddressesForLine(
    const SymbolContext& symbol_context, const FileLine& line) const {
  return std::vector<uint64_t>();
}

}  // namespace zxdb
