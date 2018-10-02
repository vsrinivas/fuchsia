// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/mock_process_symbols.h"

#include "garnet/bin/zxdb/symbols/input_location.h"
#include "garnet/bin/zxdb/symbols/line_details.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/module_symbol_status.h"

namespace zxdb {

MockProcessSymbols::MockProcessSymbols() = default;
MockProcessSymbols::~MockProcessSymbols() = default;

// ProcessSymbols implementation.
TargetSymbols* MockProcessSymbols::GetTargetSymbols() { return nullptr; }

std::vector<ModuleSymbolStatus> MockProcessSymbols::GetStatus() const {
  return std::vector<ModuleSymbolStatus>();
}

std::vector<Location> MockProcessSymbols::ResolveInputLocation(
    const InputLocation& input_location, const ResolveOptions& options) const {
  if (input_location.type == InputLocation::Type::kAddress) {
    // Always return identity for the address case.
    return std::vector<Location>{
        Location(Location::State::kAddress, input_location.address)};
  }
  // More complex stuff is not yet supported by this mock.
  return std::vector<Location>();
}

LineDetails MockProcessSymbols::LineDetailsForAddress(uint64_t address) const {
  return LineDetails();
}

std::vector<uint64_t> MockProcessSymbols::AddressesForFunction(
    const std::string& name) const {
  return std::vector<uint64_t>();
}

bool MockProcessSymbols::HaveSymbolsLoadedForModuleAt(uint64_t address) const {
  return false;
}

}  // namespace zxdb
