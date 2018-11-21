// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/mock_process_symbols.h"

#include "garnet/bin/zxdb/symbols/input_location.h"
#include "garnet/bin/zxdb/symbols/line_details.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/module_symbol_status.h"

namespace zxdb {

MockProcessSymbols::MockProcessSymbols() : weak_factory_(this) {}
MockProcessSymbols::~MockProcessSymbols() = default;

void MockProcessSymbols::AddSymbol(const std::string& name,
                                   std::vector<Location> locations) {
  symbols_[name] = std::move(locations);
}

fxl::WeakPtr<const ProcessSymbols> MockProcessSymbols::GetWeakPtr() const {
  return weak_factory_.GetWeakPtr();
}

TargetSymbols* MockProcessSymbols::GetTargetSymbols() { return nullptr; }

std::vector<ModuleSymbolStatus> MockProcessSymbols::GetStatus() const {
  return std::vector<ModuleSymbolStatus>();
}

std::vector<const LoadedModuleSymbols*>
MockProcessSymbols::GetLoadedModuleSymbols() const {
  return std::vector<const LoadedModuleSymbols*>();
}

std::vector<Location> MockProcessSymbols::ResolveInputLocation(
    const InputLocation& input_location, const ResolveOptions& options) const {
  std::vector<Location> result;
  if (input_location.type == InputLocation::Type::kAddress) {
    // Always return identity for the address case. Mark symbolized (this
    // will be stripped if necessary below).
    result.emplace_back(Location::State::kSymbolized, input_location.address);
  } else if (input_location.type == InputLocation::Type::kSymbol) {
    auto found = symbols_.find(input_location.symbol);
    if (found != symbols_.end())
      result = found->second;
  }

  if (!options.symbolize) {
    // The caller did not request symbols so convert each result to an
    // unsymbolized answer. This will match the type of output from the
    // non-mock version.
    for (size_t i = 0; i < result.size(); i++)
      result[i] = Location(Location::State::kAddress, result[i].address());
  }
  return result;
}

LineDetails MockProcessSymbols::LineDetailsForAddress(uint64_t address) const {
  return LineDetails();
}

bool MockProcessSymbols::HaveSymbolsLoadedForModuleAt(uint64_t address) const {
  return false;
}

}  // namespace zxdb
