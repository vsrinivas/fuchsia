// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/lazy_symbol.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

MockModuleSymbols::MockModuleSymbols(const std::string& local_file_name)
    : local_file_name_(local_file_name) {}
MockModuleSymbols::~MockModuleSymbols() = default;

void MockModuleSymbols::AddSymbolLocations(const std::string& name,
                                           std::vector<Location> locs) {
  symbols_[name] = std::move(locs);
}

void MockModuleSymbols::AddLineDetails(uint64_t address, LineDetails details) {
  lines_[address] = std::move(details);
}

void MockModuleSymbols::AddDieRef(const ModuleSymbolIndexNode::DieRef& die,
                                  fxl::RefPtr<Symbol> symbol) {
  die_refs_[die.offset()] = std::move(symbol);
}

void MockModuleSymbols::AddFileName(const std::string& file_name) {
  files_.push_back(file_name);
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
  std::vector<Location> result;
  switch (input_location.type) {
    case InputLocation::Type::kAddress:
      // Always return identity for the address case.
      result.emplace_back(Location::State::kAddress, input_location.address);
      break;
    case InputLocation::Type::kSymbol: {
      auto found = symbols_.find(input_location.symbol.GetFullName());
      if (found != symbols_.end())
        result = found->second;
      break;
    }
    default:
      // More complex stuff is not yet supported by this mock.
      break;
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

LineDetails MockModuleSymbols::LineDetailsForAddress(
    const SymbolContext& symbol_context, uint64_t absolute_address) const {
  // This mock assumes all addresses are absolute so the symbol context is not
  // used.
  auto found = lines_.find(absolute_address);
  if (found == lines_.end())
    return LineDetails();
  return found->second;
}

std::vector<std::string> MockModuleSymbols::FindFileMatches(
    std::string_view name) const {
  std::vector<std::string> result;
  for (const std::string& cur : files_) {
    std::string with_slash("/");
    with_slash += std::string(name);
    if (cur == name || StringEndsWith(cur, with_slash))
      result.push_back(cur);
  }
  return result;
}

const ModuleSymbolIndex& MockModuleSymbols::GetIndex() const { return index_; }

LazySymbol MockModuleSymbols::IndexDieRefToSymbol(
    const ModuleSymbolIndexNode::DieRef& die_ref) const {
  auto found = die_refs_.find(die_ref.offset());
  if (found == die_refs_.end())
    return LazySymbol();
  return LazySymbol(found->second);
}

}  // namespace zxdb
