// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/mock_module_symbols.h"

#include "garnet/bin/zxdb/symbols/input_location.h"
#include "garnet/bin/zxdb/symbols/line_details.h"
#include "garnet/bin/zxdb/symbols/location.h"

namespace zxdb {

MockModuleSymbols::MockModuleSymbols(const std::string& local_file_name)
    : local_file_name_(local_file_name) {}
MockModuleSymbols::~MockModuleSymbols() = default;

void MockModuleSymbols::AddSymbol(const std::string& name,
                                  std::vector<uint64_t> addrs) {
  symbols_[name] = std::move(addrs);
}

void MockModuleSymbols::AddLineDetails(uint64_t address, LineDetails details) {
  lines_[address] = std::move(details);
}

ModuleSymbolStatus MockModuleSymbols::GetStatus() const {
  ModuleSymbolStatus status;
  status.name = local_file_name_;
  status.functions_indexed = symbols_.size();
  status.symbols_loaded = true;
  return status;
}

std::vector<Location> MockModuleSymbols::ResolveInputLocation(
    const SymbolContext& symbol_context, const InputLocation& input_location,
    const ResolveOptions& options) const {
  switch (input_location.type) {
    case InputLocation::Type::kAddress:
      // Always return identity for the address case.
      return std::vector<Location>{
          Location(Location::State::kAddress, input_location.address)};
    case InputLocation::Type::kSymbol: {
      auto found = symbols_.find(input_location.symbol);

      std::vector<Location> result;
      if (found == symbols_.end())
        return result;
      for (uint64_t address : found->second)
        result.push_back(Location(Location::State::kSymbolized, address));
      return result;
    }
    default:
      // More complex stuff is not yet supported by this mock.
      return std::vector<Location>();
  }
}

LineDetails MockModuleSymbols::LineDetailsForAddress(
    const SymbolContext& symbol_context, uint64_t absolute_address) const {
  // This mock assumes all addresses are absolute so the symbol context is not
  // used.
  auto found = lines_.find(absolute_address);
  if (found == lines_.end())
    return LineDetails();
  return found->second;
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

}  // namespace zxdb
