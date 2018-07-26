// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/process_symbols_impl.h"

#include "garnet/bin/zxdb/client/symbols/line_details.h"
#include "garnet/bin/zxdb/client/symbols/loaded_module_symbols.h"
#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"
#include "garnet/bin/zxdb/client/symbols/system_symbols.h"
#include "garnet/bin/zxdb/client/symbols/target_symbols_impl.h"
#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

namespace {

// The vDSO doesn't have symbols and we don't want to give error messages for
// it. Ignore failures for modules that this returns true for.
bool ExpectSymbolsForName(const std::string& name) { return name != "<vDSO>"; }

}  // namespace

ProcessSymbolsImpl::ProcessSymbolsImpl(Notifications* notifications,
                                       TargetSymbolsImpl* target_symbols)
    : notifications_(notifications), target_symbols_(target_symbols) {}

ProcessSymbolsImpl::~ProcessSymbolsImpl() = default;

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
  std::vector<Err> load_errors;
  for (const auto& added_index : new_module_indices) {
    Err sym_load_err;
    ModuleInfo* info = SaveModuleInfo(modules[added_index], &sym_load_err);
    if (sym_load_err.has_error())
      load_errors.push_back(std::move(sym_load_err));
    else if (info->symbols)
      added_modules.push_back(info->symbols.get());
  }

  // Update the TargetSymbols.
  target_symbols_->RemoveAllModules();
  for (auto& pair : modules_) {
    if (pair.second.symbols) {
      target_symbols_->AddModule(fxl::RefPtr<SystemSymbols::ModuleRef>(
          pair.second.symbols->module_ref()));
    }
  }

  // Send notifications last so everything is in a consistent state.
  for (auto& added_module : added_modules)
    notifications_->DidLoadModuleSymbols(added_module);
  for (auto& err : load_errors)
    notifications_->OnSymbolLoadFailure(err);
}

TargetSymbols* ProcessSymbolsImpl::GetTargetSymbols() {
  return target_symbols_;
}

std::vector<ModuleSymbolStatus> ProcessSymbolsImpl::GetStatus() const {
  std::vector<ModuleSymbolStatus> result;
  for (const auto& pair : modules_) {
    if (pair.second.symbols.get()) {
      result.push_back(pair.second.symbols->module_symbols()->GetStatus());
      // ModuleSymbols doesn't know the name or base address so fill in now.
      result.back().name = pair.second.name;
      result.back().base = pair.second.base;
    } else {
      // No symbols, make an empty record.
      ModuleSymbolStatus status;
      status.name = pair.second.name;
      status.build_id = pair.second.build_id;
      status.base = pair.second.base;
      status.symbols_loaded = false;
      result.push_back(std::move(status));
    }
  }
  return result;
}

Location ProcessSymbolsImpl::LocationForAddress(uint64_t address) const {
  const ModuleInfo* info = InfoForAddress(address);
  if (!info || !info->symbols)
    return Location(Location::State::kSymbolized, address);  // Can't symbolize.
  return info->symbols->module_symbols()->LocationForAddress(
      info->symbols->symbol_context(), address);
}

LineDetails ProcessSymbolsImpl::LineDetailsForAddress(uint64_t address) const {
  const ModuleInfo* info = InfoForAddress(address);
  if (!info || !info->symbols)
    return LineDetails();
  return info->symbols->module_symbols()->LineDetailsForAddress(
      info->symbols->symbol_context(), address);
}

std::vector<uint64_t> ProcessSymbolsImpl::AddressesForFunction(
    const std::string& name) const {
  std::vector<uint64_t> result;
  for (const auto& pair : modules_) {
    if (pair.second.symbols) {
      const LoadedModuleSymbols* loaded = pair.second.symbols.get();
      for (auto local_addr : loaded->module_symbols()->AddressesForFunction(
               loaded->symbol_context(), name))
        result.push_back(local_addr);
    }
  }
  return result;
}

std::vector<uint64_t> ProcessSymbolsImpl::AddressesForLine(
    const FileLine& line) const {
  std::vector<uint64_t> result;
  for (const auto& pair : modules_) {
    if (pair.second.symbols) {
      const LoadedModuleSymbols* loaded = pair.second.symbols.get();
      for (auto local_addr : loaded->module_symbols()->AddressesForLine(
               loaded->symbol_context(), line))
        result.push_back(local_addr);
    }
  }
  return result;
}

ProcessSymbolsImpl::ModuleInfo* ProcessSymbolsImpl::SaveModuleInfo(
    const debug_ipc::Module& module, Err* symbol_load_err) {
  ModuleInfo info;
  info.name = module.name;
  info.build_id = module.build_id;
  info.base = module.base;

  fxl::RefPtr<SystemSymbols::ModuleRef> module_symbols;
  *symbol_load_err = target_symbols_->system_symbols()->GetModule(
      module.name, module.build_id, &module_symbols);
  if (symbol_load_err->has_error()) {
    // Error, but it may be expected.
    if (!ExpectSymbolsForName(module.name))
      *symbol_load_err = Err();
  } else {
    // Success, make the LoadedModuleSymbols.
    info.symbols = std::make_unique<LoadedModuleSymbols>(
        std::move(module_symbols), module.base);
  }

  auto inserted_iter =
      modules_
          .emplace(std::piecewise_construct, std::forward_as_tuple(module.base),
                   std::forward_as_tuple(std::move(info)))
          .first;
  return &inserted_iter->second;
}

// static
bool ProcessSymbolsImpl::RefersToSameModule(const debug_ipc::Module& a,
                                            const ModuleInfo& b) {
  return a.base == b.base && a.build_id == b.build_id;
}

const ProcessSymbolsImpl::ModuleInfo* ProcessSymbolsImpl::InfoForAddress(
    uint64_t address) const {
  if (modules_.empty())
    return nullptr;
  auto found = modules_.lower_bound(address);
  if (found == modules_.end() || found->first > address) {
    if (found == modules_.begin())
      return nullptr;  // Address below first module.
    // Move to previous item to get the module starting before this address.
    --found;
  }
  return &found->second;
}

}  // namespace zxdb
