// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/process_symbols.h"

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/largest_less_or_equal.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/developer/debug/zxdb/symbols/system_symbols.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"

namespace zxdb {

namespace {

// The vDSO doesn't have symbols and we don't want to give error messages for
// it. Ignore failures for modules that this returns true for.
bool ExpectSymbolsForName(const std::string& name) { return name != "<vDSO>"; }

}  // namespace

ProcessSymbols::ProcessSymbols(Notifications* notifications, TargetSymbols* target_symbols)
    : notifications_(notifications), target_symbols_(target_symbols), weak_factory_(this) {}

ProcessSymbols::~ProcessSymbols() = default;

fxl::WeakPtr<const ProcessSymbols> ProcessSymbols::GetWeakPtr() const {
  return weak_factory_.GetWeakPtr();
}

void ProcessSymbols::SetModules(const std::vector<debug_ipc::Module>& modules) {
  // Map from load address to index into |modules| argument.
  std::map<uint64_t, size_t> new_module_address_to_index;

  // Find new modules. These are indices into |modules| of the added ones.
  std::vector<size_t> new_module_indices;
  for (size_t i = 0; i < modules.size(); i++) {
    new_module_address_to_index[modules[i].base] = i;

    auto found_addr = modules_.find(modules[i].base);
    // Even if address is a match, the library could have been swapped.
    if (found_addr == modules_.end() || !RefersToSameModule(modules[i], found_addr->second))
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
    else if (info->symbols->module_symbols_ref())
      added_modules.push_back(info->symbols.get());
  }

  DoRefreshTargetSymbols();

  // Send notifications last so everything is in a consistent state.
  for (auto& added_module : added_modules)
    notifications_->DidLoadModuleSymbols(added_module);
  for (auto& err : load_errors)
    notifications_->OnSymbolLoadFailure(err);
}

void ProcessSymbols::InjectModuleForTesting(const std::string& name, const std::string& build_id,
                                            std::unique_ptr<LoadedModuleSymbols> mod_sym) {
  LoadedModuleSymbols* loaded_ptr = mod_sym.get();  // Save for after being moved out below.
  FX_DCHECK(loaded_ptr);

  ModuleInfo info;
  info.name = name;
  info.build_id = build_id;
  info.base = loaded_ptr->load_address();
  info.symbols = std::move(mod_sym);
  modules_[loaded_ptr->load_address()] = std::move(info);

  // Issue notifications.
  target_symbols_->AddModule(loaded_ptr->module_symbols_ref());
  notifications_->DidLoadModuleSymbols(loaded_ptr);
}

std::vector<ModuleSymbolStatus> ProcessSymbols::GetStatus() const {
  std::vector<ModuleSymbolStatus> result;
  for (const auto& [base, mod_info] : modules_) {
    if (mod_info.symbols->module_symbols()) {
      result.push_back(mod_info.symbols->module_symbols()->GetStatus());
      // ModuleSymbols doesn't know the name or base address so fill in now.
      result.back().name = mod_info.name;
      result.back().base = mod_info.base;
      result.back().symbols = mod_info.symbols.get();
    } else {
      // No symbols, make an empty record.
      ModuleSymbolStatus status;
      status.name = mod_info.name;
      status.build_id = mod_info.build_id;
      status.base = mod_info.base;
      status.symbols_loaded = false;
      result.push_back(std::move(status));
    }
  }
  return result;
}

std::vector<const LoadedModuleSymbols*> ProcessSymbols::GetLoadedModuleSymbols() const {
  std::vector<const LoadedModuleSymbols*> result;
  result.reserve(modules_.size());
  for (const auto& [base, mod_info] : modules_) {
    if (mod_info.symbols->module_symbols())
      result.push_back(mod_info.symbols.get());
  }
  return result;
}

const LoadedModuleSymbols* ProcessSymbols::GetLoadedForModuleSymbols(
    const ModuleSymbols* mod_sym) const {
  for (const auto& [addr, info] : modules_) {
    if (info.symbols->module_symbols() == mod_sym)
      return info.symbols.get();
  }
  return nullptr;
}

const LoadedModuleSymbols* ProcessSymbols::GetModuleForAddress(uint64_t address) const {
  const ModuleInfo* info = InfoForAddress(address);
  if (!info)
    return nullptr;
  return info->symbols.get();
}

LoadedModuleSymbols* ProcessSymbols::GetModuleForAddress(uint64_t address) {
  return const_cast<LoadedModuleSymbols*>(
      const_cast<const ProcessSymbols*>(this)->GetModuleForAddress(address));
}

std::vector<Location> ProcessSymbols::ResolveInputLocation(const InputLocation& input_location,
                                                           const ResolveOptions& options) const {
  FX_DCHECK(input_location.type != InputLocation::Type::kNone);

  // Address resolution.
  if (input_location.type == InputLocation::Type::kAddress) {
    if (options.symbolize) {
      // Symbolize one address.
      const ModuleInfo* info = InfoForAddress(input_location.address);
      if (!info || !info->symbols->module_symbols()) {
        // Can't symbolize.
        return std::vector<Location>{
            Location(Location::State::kSymbolized, input_location.address)};
      }
      // Have the module the address.
      return info->symbols->ResolveInputLocation(input_location, options);
    }

    // No-op conversion of address -> address.
    return std::vector<Location>{Location(Location::State::kAddress, input_location.address)};
  }

  // Symbol and file/line resolution both requires iterating over all modules.
  std::vector<Location> result;
  for (const auto& [base, mod_info] : modules_) {
    if (mod_info.symbols->module_symbols()) {
      const LoadedModuleSymbols* loaded = mod_info.symbols.get();
      for (Location& location : loaded->ResolveInputLocation(input_location, options))
        result.push_back(std::move(location));
    }
  }
  return result;
}

LineDetails ProcessSymbols::LineDetailsForAddress(uint64_t address, bool greedy) const {
  const ModuleInfo* info = InfoForAddress(address);
  if (!info || !info->symbols->module_symbols_ref())
    return LineDetails();
  return info->symbols->module_symbols()->LineDetailsForAddress(info->symbols->symbol_context(),
                                                                address, greedy);
}

bool ProcessSymbols::HaveSymbolsLoadedForModuleAt(uint64_t address) const {
  const ModuleInfo* info = InfoForAddress(address);
  return info && info->symbols->module_symbols();
}

ProcessSymbols::ModuleInfo* ProcessSymbols::SaveModuleInfo(const debug_ipc::Module& module,
                                                           Err* symbol_load_err) {
  ModuleInfo info;
  info.name = module.name;
  info.build_id = module.build_id;
  info.base = module.base;

  fxl::RefPtr<ModuleSymbols> module_symbols;
  *symbol_load_err = target_symbols_->system_symbols()->GetModule(module.build_id, &module_symbols);
  if (symbol_load_err->has_error()) {
    // Error, but it may be expected.
    if (!ExpectSymbolsForName(module.name))
      *symbol_load_err = Err();
    info.symbols = std::make_unique<LoadedModuleSymbols>(nullptr, module.build_id, module.base,
                                                         module.debug_address);
  } else {
    // Success, make the LoadedModuleSymbols.
    info.symbols = std::make_unique<LoadedModuleSymbols>(std::move(module_symbols), module.build_id,
                                                         module.base, module.debug_address);
  }

  auto inserted_iter = modules_
                           .emplace(std::piecewise_construct, std::forward_as_tuple(module.base),
                                    std::forward_as_tuple(std::move(info)))
                           .first;
  return &inserted_iter->second;
}

void ProcessSymbols::RetryLoadBuildID(const std::string& build_id, DebugSymbolFileType file_type) {
  auto download_type = SystemSymbols::DownloadType::kNone;

  if (file_type == DebugSymbolFileType::kDebugInfo) {
    download_type = SystemSymbols::DownloadType::kBinary;
  }

  for (auto& [base, mod] : modules_) {
    if (mod.build_id != build_id) {
      continue;
    }

    fxl::RefPtr<ModuleSymbols> module_symbols;
    Err err =
        target_symbols_->system_symbols()->GetModule(build_id, &module_symbols, download_type);

    if (!err.has_error() && !module_symbols) {
      err = Err("Symbols were downloaded but did not appear in index.");
    }

    if (err.has_error()) {
      notifications_->OnSymbolLoadFailure(err);
      return;
    }

    mod.symbols = std::make_unique<LoadedModuleSymbols>(std::move(module_symbols), build_id, base,
                                                        mod.symbols->debug_address());

    // If we can have multiple modules with the same build ID in the process then this logic will
    // be wrong. I don't see how that happens as of today, though.
    DoRefreshTargetSymbols();
    notifications_->DidLoadModuleSymbols(mod.symbols.get());
    return;
  }
}

void ProcessSymbols::DoRefreshTargetSymbols() {
  // Update the TargetSymbols.
  target_symbols_->RemoveAllModules();
  for (auto& [base, mod_info] : modules_) {
    if (mod_info.symbols->module_symbols_ref())
      target_symbols_->AddModule(mod_info.symbols->module_symbols_ref());
  }
}

// static
bool ProcessSymbols::RefersToSameModule(const debug_ipc::Module& a, const ModuleInfo& b) {
  return a.base == b.base && a.build_id == b.build_id;
}

const ProcessSymbols::ModuleInfo* ProcessSymbols::InfoForAddress(uint64_t address) const {
  if (modules_.empty())
    return nullptr;

  // TODO(bug 42243) we should be able to tell the size of the module and fail when it's outside
  // the extent of one.
  auto found = debug_ipc::LargestLessOrEqual(
      modules_.begin(), modules_.end(), address,
      [](const ModuleMap::value_type& v, uint64_t a) { return v.first < a; },
      [](const ModuleMap::value_type& v, uint64_t a) { return v.first == a; });
  if (found == modules_.end())
    return nullptr;  // Address below first module.
  return &found->second;
}

}  // namespace zxdb
