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

ProcessSymbolsImpl::ProcessSymbolsImpl(Notifications* notifications,
                                       TargetSymbolsImpl* target_symbols)
    : notifications_(notifications),
      target_symbols_(target_symbols) {}

ProcessSymbolsImpl::~ProcessSymbolsImpl() = default;

void ProcessSymbolsImpl::AddModule(
    const debug_ipc::Module& module,
    std::function<void(const std::string&)> callback) {
  // TODO(brettw) support run-time dynamic module loading notifications.
}

void ProcessSymbolsImpl::SetModules(
    const std::vector<debug_ipc::Module>& modules) {
  // Map from load address to index into |modules| argument.
  std::map<uint64_t, size_t> new_module_address_to_index;

  // Find new modules. These are indices into |modules| of the added ones.
  std::vector<size_t> new_module_indices;
  for (size_t i = 0; i < modules.size(); i++) {
    new_module_address_to_index[modules[i].base] = i;

    auto found_addr = modules_.find(modules[i].base);
    // Even if address is a match, the library could have been swapped.
    if (found_addr == modules_.end() ||
        !RefersToSameModule(modules[i], found_addr->second))
      new_module_indices.push_back(i);
  }

  // Find deleted modules and remove them.
  std::vector<ModuleMap::iterator> deleted_modules;
  for (auto iter = modules_.begin(); iter != modules_.end(); ++iter) {
    auto found_index = new_module_address_to_index.find(iter->second.base);
    if (found_index == new_module_address_to_index.end() ||
        !RefersToSameModule(modules[found_index->second], iter->second))
      deleted_modules.push_back(iter);
  }

  // First update for deleted modules since the addresses may overlap the
  // added ones.
  for (auto& deleted : deleted_modules) {
    notifications_->WillUnloadModuleSymbols(deleted->second.symbols.get());
    modules_.erase(deleted);
  }
  deleted_modules.clear();

  // Process the added ones.
  std::vector<LoadedModuleSymbols*> added_modules;
  SystemSymbols* system_symbols = target_symbols_->system_symbols();
  for (const auto& added_index : new_module_indices) {
    const debug_ipc::Module& module = modules[added_index];

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
      added_modules.push_back(info.symbols.get());
    } else if (ExpectSymbolsForName(module.name)) {
      notifications_->OnSymbolLoadFailure(err);
    }
    modules_.emplace(std::piecewise_construct,
                     std::forward_as_tuple(module.base),
                     std::forward_as_tuple(std::move(info)));
  }

  // Update the TargetSymbols.
  target_symbols_->RemoveAllModules();
  for (auto& pair : modules_) {
    if (pair.second.symbols)
      target_symbols_->AddModule(pair.second.symbols->module());
  }

  // Send add notifications last so everything is in a consistent state.
  for (auto& added_module : added_modules)
    notifications_->DidLoadModuleSymbols(added_module);
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

// static
bool ProcessSymbolsImpl::RefersToSameModule(const debug_ipc::Module& a,
                                            const ModuleInfo& b) {
  return a.base == b.base && a.build_id == b.build_id;
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
