// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/target_symbols_impl.h"

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
    : system_symbols_(other.system_symbols_),
      modules_(other.modules_) {}
TargetSymbolsImpl::~TargetSymbolsImpl() {}

TargetSymbolsImpl& TargetSymbolsImpl::operator=(const TargetSymbolsImpl& other) {
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

void TargetSymbolsImpl::RemoveAllModules() {
  modules_.clear();
}

}  // namespace zxdb
