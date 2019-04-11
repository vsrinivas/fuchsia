// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/target_symbols.h"

#include <set>

#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"
#include "src/lib/fxl/strings/split_string.h"

namespace zxdb {

// Does a pointer-identity comparison of two ModuleRefs.
bool TargetSymbols::ModuleRefComparePtr::operator()(
    const fxl::RefPtr<SystemSymbols::ModuleRef>& a,
    const fxl::RefPtr<SystemSymbols::ModuleRef>& b) const {
  return a.get() < b.get();
}

TargetSymbols::TargetSymbols(SystemSymbols* system_symbols)
    : system_symbols_(system_symbols) {}
TargetSymbols::TargetSymbols(const TargetSymbols& other)
    : system_symbols_(other.system_symbols_), modules_(other.modules_) {}
TargetSymbols::~TargetSymbols() {}

TargetSymbols& TargetSymbols::operator=(const TargetSymbols& other) {
  modules_ = other.modules_;
  return *this;
}

void TargetSymbols::AddModule(fxl::RefPtr<SystemSymbols::ModuleRef> module) {
  modules_.insert(std::move(module));
}

void TargetSymbols::RemoveModule(
    fxl::RefPtr<SystemSymbols::ModuleRef>& module) {
  auto found = modules_.find(module);
  if (found == modules_.end()) {
    FXL_NOTREACHED();
    return;
  }
  modules_.erase(found);
}

void TargetSymbols::RemoveAllModules() { modules_.clear(); }

std::vector<const ModuleSymbols*> TargetSymbols::GetModuleSymbols() const {
  std::vector<const ModuleSymbols*> result;
  for (const auto& module : modules_)
    result.push_back(module->module_symbols());
  return result;
}

std::vector<Location> TargetSymbols::ResolveInputLocation(
    const InputLocation& input_location, const ResolveOptions& options) const {
  FXL_DCHECK(input_location.type != InputLocation::Type::kNone);
  FXL_DCHECK(input_location.type != InputLocation::Type::kAddress);

  // This uses a null symbol context since this function doesn't depend on
  // any actual locations of libraries in memory.
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  std::vector<Location> result;
  for (const auto& module : modules_) {
    for (const Location& location :
         module->module_symbols()->ResolveInputLocation(
             symbol_context, input_location, ResolveOptions())) {
      // Clear the location on the result to prevent confusion.
      result.emplace_back(0, location.file_line(), location.column(),
                          location.symbol_context(), location.symbol());
    }
  }
  return result;
}

std::vector<std::string> TargetSymbols::FindFileMatches(
    std::string_view name) const {
  // Different modules can each use the same file, but we want to return each
  // one once.
  std::set<std::string> result_set;
  for (const auto& module : modules_) {
    for (auto& file : module->module_symbols()->FindFileMatches(name))
      result_set.insert(std::move(file));
  }

  std::vector<std::string> result;
  for (auto& cur : result_set)
    result.push_back(std::move(cur));
  return result;
}

// This could be optimized quite a bit if we find it's slow, there are a lot
// of container copies in this implementation.
std::string TargetSymbols::GetShortestUniqueFileName(
    std::string_view file_name) const {
  if (file_name.empty())
    return std::string();

  // Get all matches for just the file name part.
  std::string_view file_name_last_part = ExtractLastFileComponent(file_name);
  std::vector<std::string> all_matches = FindFileMatches(file_name_last_part);
  if (all_matches.size() <= 1)
    return std::string(file_name_last_part);  // Unique or not found.

  auto components =
      fxl::SplitString(fxl::StringView(file_name.data(), file_name.size()), "/",
                       fxl::kKeepWhitespace, fxl::kSplitWantAll);

  // Append path components from the right until its unique.
  std::string result(file_name_last_part);
  for (int comp_index = static_cast<int>(components.size()) - 2;
       comp_index >= 0; comp_index--) {
    result = components[comp_index].ToString() + "/" + result;
    if (FindFileMatches(result).size() <= 1)
      return result;
  }
  return result;
}

}  // namespace zxdb
