// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/process_symbols_impl.h"

#include "garnet/bin/zxdb/client/symbols/loaded_module_symbols_impl.h"
#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"
#include "garnet/bin/zxdb/client/symbols/system_symbols.h"
#include "garnet/bin/zxdb/client/symbols/target_symbols_impl.h"
#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

namespace {

// The vDSO doesn't have symbols and we don't want to give error messages for
// it. Ignore failures for modules that this returns true for.
bool ExpectSymbolsForName(const std::string& name) {
  return name != "<vDSO>";
}

}  // namespace

ProcessSymbolsImpl::ProcessSymbolsImpl(TargetSymbolsImpl* target_symbols)
    : target_symbols_(target_symbols) {}

ProcessSymbolsImpl::~ProcessSymbolsImpl() = default;

void ProcessSymbolsImpl::AddModule(
    const debug_ipc::Module& module,
    std::function<void(const std::string&)> callback) {
  // TODO(brettw) support run-time dynamic module loading notifications.
}

void ProcessSymbolsImpl::SetModules(
    const std::vector<debug_ipc::Module>& modules) {
  // This function must be careful not to delete any references to old modules
  // that might be re-used in the new set. This variable keeps them in scope
  // while we generate the new map.
  auto old_modules = std::move(modules_);

  SystemSymbols* system_symbols = target_symbols_->system_symbols();
  for (const debug_ipc::Module& module : modules) {
    ModuleInfo info;
    info.name = module.name;
    info.build_id = module.build_id;
    info.base = module.base;

    fxl::RefPtr<SystemSymbols::ModuleRef> module_symbols;
    Err err = system_symbols->GetModule(
        module.name, module.build_id, &module_symbols);
    if (!err.has_error()) {
      // Success, make the LoadedModuleSymbolsImpl.
      info.symbols = std::make_unique<LoadedModuleSymbolsImpl>(
          std::move(module_symbols), module.base);
    } else if (symbol_load_failure_callback_ &&
               ExpectSymbolsForName(module.name)) {
      symbol_load_failure_callback_(err);
    }
    modules_.emplace(std::piecewise_construct,
                     std::forward_as_tuple(module.base),
                     std::forward_as_tuple(std::move(info)));
  }

  // Update the TargetSymbols last. It may have been keeping an old
  // ModuleSymbols object alive that was needed above.
  target_symbols_->RemoveAllModules();
  for (auto& pair : modules_) {
    if (pair.second.symbols)
      target_symbols_->AddModule(pair.second.symbols->module());
  }
}

TargetSymbols* ProcessSymbolsImpl::GetTargetSymbols() {
  return target_symbols_;
}

std::vector<ProcessSymbols::ModuleStatus> ProcessSymbolsImpl::GetStatus() const {
  std::vector<ModuleStatus> result;
  for (const auto& pair : modules_) {
    ModuleStatus status;
    status.name = pair.second.name;
    status.build_id = pair.second.build_id;
    status.base = pair.second.base;
    status.symbols_loaded = pair.second.symbols.get();
    if (pair.second.symbols) {
      status.symbol_file =
          pair.second.symbols->GetModuleSymbols()->GetLocalFileName();
    }
    result.push_back(std::move(status));
  }
  return result;
}

Location ProcessSymbolsImpl::GetLocationForAddress(uint64_t address) const {
  const ModuleInfo* info = InfoForAddress(address);
  if (!info || !info->symbols)
    return Location(Location::State::kSymbolized, address);  // Can't symbolize.
  return info->symbols->LocationForAddress(address);
}

std::vector<uint64_t> ProcessSymbolsImpl::GetAddressesForFunction(
    const std::string& name) const {
  std::vector<uint64_t> result;

  for (const auto& pair : modules_) {
    if (pair.second.symbols) {
      for (auto local_addr : pair.second.symbols->AddressesForFunction(name))
        result.push_back(local_addr);
    }
  }

  return result;
}

const ProcessSymbolsImpl::ModuleInfo* ProcessSymbolsImpl::InfoForAddress(
    uint64_t address) const {
  auto found = modules_.lower_bound(address);
  if (found->first > address) {
    if (found == modules_.begin())
      return nullptr;  // Address below first module.
    // Move to previous item to get the module starting before this address.
    --found;
  }
  return &found->second;
}

}  // namespace zxdb
