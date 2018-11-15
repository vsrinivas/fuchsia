// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/target_symbols_impl.h"

#include <set>

#include "garnet/bin/zxdb/symbols/input_location.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/module_symbols.h"
#include "garnet/bin/zxdb/symbols/resolve_options.h"

namespace zxdb {

// Does a pointer-identity comparison of two ModuleRefs.
bool TargetSymbolsImpl::ModuleRefComparePtr::operator()(
    const fxl::RefPtr<SystemSymbols::ModuleRef>& a,
    const fxl::RefPtr<SystemSymbols::ModuleRef>& b) const {
  return a.get() < b.get();
}

TargetSymbolsImpl::TargetSymbolsImpl(SystemSymbols* system_symbols)
    : system_symbols_(system_symbols) {}
TargetSymbolsImpl::TargetSymbolsImpl(const TargetSymbolsImpl& other)
    : system_symbols_(other.system_symbols_), modules_(other.modules_) {}
TargetSymbolsImpl::~TargetSymbolsImpl() {}

TargetSymbolsImpl& TargetSymbolsImpl::operator=(
    const TargetSymbolsImpl& other) {
  modules_ = other.modules_;
  return *this;
}

void TargetSymbolsImpl::AddModule(
    fxl::RefPtr<SystemSymbols::ModuleRef> module) {
  modules_.insert(std::move(module));
}

void TargetSymbolsImpl::RemoveModule(
    fxl::RefPtr<SystemSymbols::ModuleRef>& module) {
  auto found = modules_.find(module);
  if (found == modules_.end()) {
    FXL_NOTREACHED();
    return;
  }
  modules_.erase(found);
}

void TargetSymbolsImpl::RemoveAllModules() { modules_.clear(); }

std::vector<Location> TargetSymbolsImpl::ResolveInputLocation(
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

std::vector<std::string> TargetSymbolsImpl::FindFileMatches(
    const std::string& name) const {
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

}  // namespace zxdb
