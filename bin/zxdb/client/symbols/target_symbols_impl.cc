// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/target_symbols_impl.h"

#include <set>

#include "garnet/bin/zxdb/client/symbols/module_symbols.h"

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

std::vector<FileLine> TargetSymbolsImpl::FindLinesForSymbol(
    const std::string& name) const {
  // Inline functions will have multiple locations but the same FileLine, which
  // we only want to return once.
  std::set<FileLine> result_set;

  // This uses a null symbol context since this function doesn't depend on
  // any actual locations of libraries in memory.
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  std::vector<uint64_t> addrs;
  for (const auto& module : modules_) {
    addrs =
        module->module_symbols()->AddressesForFunction(symbol_context, name);

    // Convert each address back into a location to get its file/line.
    for (uint64_t addr : addrs) {
      Location loc =
          module->module_symbols()->LocationForAddress(symbol_context, addr);
      if (loc.has_symbols())
        result_set.insert(loc.file_line());
    }
  }

  std::vector<FileLine> result;
  for (auto& cur : result_set)
    result.push_back(std::move(cur));
  return result;
}

}  // namespace zxdb
