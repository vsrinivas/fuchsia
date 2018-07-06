// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/mock_process_symbols.h"

#include "garnet/bin/zxdb/client/symbols/line_details.h"
#include "garnet/bin/zxdb/client/symbols/location.h"

namespace zxdb {

MockProcessSymbols::MockProcessSymbols() = default;
MockProcessSymbols::~MockProcessSymbols() = default;

// ProcessSymbols implementation.
TargetSymbols* MockProcessSymbols::GetTargetSymbols() { return nullptr; }

std::vector<ProcessSymbols::ModuleStatus> MockProcessSymbols::GetStatus()
    const {
  return std::vector<ModuleStatus>();
}

Location MockProcessSymbols::LocationForAddress(uint64_t address) const {
  return Location(Location::State::kSymbolized, address);
}

LineDetails MockProcessSymbols::LineDetailsForAddress(uint64_t address) const {
  return LineDetails();
}

std::vector<uint64_t> MockProcessSymbols::AddressesForFunction(
    const std::string& name) const {
  return std::vector<uint64_t>();
}

std::vector<uint64_t> MockProcessSymbols::AddressesForLine(
    const FileLine& line) const {
  return std::vector<uint64_t>();
}

}  // namespace zxdb
