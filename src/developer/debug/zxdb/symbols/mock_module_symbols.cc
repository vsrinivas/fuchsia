// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/lazy_symbol.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/test_symbol_module.h"

namespace zxdb {

MockModuleSymbols::MockModuleSymbols(const std::string& local_file_name)
    : local_file_name_(local_file_name) {}
MockModuleSymbols::~MockModuleSymbols() = default;

void MockModuleSymbols::AddSymbolLocations(const Identifier& name, std::vector<Location> locs) {
  named_symbols_[name] = std::move(locs);
}

void MockModuleSymbols::AddSymbolLocations(const std::string& name, std::vector<Location> locs) {
  named_symbols_[TestSymbolModule::SplitName(name)] = std::move(locs);
}

void MockModuleSymbols::AddSymbolLocations(uint64_t address, std::vector<Location> locs) {
  addr_symbols_[address] = std::move(locs);
}

void MockModuleSymbols::AddLineDetails(uint64_t address, LineDetails details) {
  lines_[address] = std::move(details);
}

void MockModuleSymbols::AddSymbolRef(const IndexNode::SymbolRef& die, fxl::RefPtr<Symbol> symbol) {
  die_refs_[die.offset()] = std::move(symbol);
}

void MockModuleSymbols::AddFileName(const std::string& file_name) { files_.push_back(file_name); }

ModuleSymbolStatus MockModuleSymbols::GetStatus() const {
  ModuleSymbolStatus status;
  status.name = local_file_name_;
  status.functions_indexed = named_symbols_.size();
  status.symbols_loaded = true;
  return status;
}

std::vector<Location> MockModuleSymbols::ResolveInputLocation(const SymbolContext& symbol_context,
                                                              const InputLocation& input_location,
                                                              const ResolveOptions& options) const {
  std::vector<Location> result;
  switch (input_location.type) {
    case InputLocation::Type::kAddress:
      if (auto found = addr_symbols_.find(input_location.address); found != addr_symbols_.end()) {
        result = found->second;
      } else {
        // Return identity for all non-found addresses.
        result.emplace_back(Location::State::kAddress, input_location.address);
      }
      break;
    case InputLocation::Type::kName: {
      // The input may be qualified or unqualified globally. Allow either to match an unqualified
      // expected value.
      if (auto found = named_symbols_.find(input_location.name); found != named_symbols_.end()) {
        result = found->second;
      } else if (input_location.name.qualification() == IdentifierQualification::kGlobal) {
        // Try looking up after removing the global qualifier.
        Identifier unqualified = input_location.name;
        unqualified.set_qualification(IdentifierQualification::kRelative);
        if (auto found = named_symbols_.find(unqualified); found != named_symbols_.end())
          result = found->second;
      }
      break;
    }
    default:
      // More complex stuff is not yet supported by this mock.
      break;
  }

  if (!options.symbolize) {
    // The caller did not request symbols so convert each result to an unsymbolized answer. This
    // will match the type of output from the non-mock version.
    for (size_t i = 0; i < result.size(); i++)
      result[i] = Location(Location::State::kAddress, result[i].address());
  }
  return result;
}

fxl::RefPtr<DwarfUnit> MockModuleSymbols::GetDwarfUnit(const SymbolContext& symbol_context,
                                                       uint64_t absolute_address) const {
  return nullptr;
}

LineDetails MockModuleSymbols::LineDetailsForAddress(const SymbolContext& symbol_context,
                                                     uint64_t absolute_address, bool greedy) const {
  // This mock assumes all addresses are absolute so the symbol context is not used.
  auto found = lines_.find(absolute_address);
  if (found == lines_.end())
    return LineDetails();
  return found->second;
}

std::vector<std::string> MockModuleSymbols::FindFileMatches(std::string_view name) const {
  std::vector<std::string> result;
  for (const std::string& cur : files_) {
    std::string with_slash("/");
    with_slash += std::string(name);
    if (cur == name || StringEndsWith(cur, with_slash))
      result.push_back(cur);
  }
  return result;
}

std::vector<fxl::RefPtr<Function>> MockModuleSymbols::GetMainFunctions() const {
  return std::vector<fxl::RefPtr<Function>>();
}

const Index& MockModuleSymbols::GetIndex() const { return index_; }

LazySymbol MockModuleSymbols::IndexSymbolRefToSymbol(const IndexNode::SymbolRef& die_ref) const {
  auto found = die_refs_.find(die_ref.offset());
  if (found == die_refs_.end())
    return LazySymbol();
  return found->second;
}

bool MockModuleSymbols::HasBinary() const { return false; }

}  // namespace zxdb
