// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/process_symbols_impl.h"

#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/symbols/module_symbols.h"
#include "garnet/bin/zxdb/client/system_impl.h"
#include "garnet/bin/zxdb/client/system_symbols.h"
#include "garnet/bin/zxdb/client/target_impl.h"
#include "garnet/bin/zxdb/client/target_symbols_impl.h"
#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

namespace {

// The vDSO doesn't have symbols and we don't want to give error messages for
// it. Ignore failures for modules that this returns true for.
bool ExpectSymbolsForName(const std::string& name) {
  return name != "<vDSO>";
}

}  // namespace

ProcessSymbolsImpl::ProcessSymbolsImpl(ProcessImpl* process)
    : ProcessSymbols(process->session()), process_(process) {}

ProcessSymbolsImpl::~ProcessSymbolsImpl() {}

void ProcessSymbolsImpl::AddModule(
    const debug_ipc::Module& module,
    std::function<void(const std::string&)> callback) {
  // TODO(brettw) support run-time dynamic module loading notifications.
}

void ProcessSymbolsImpl::SetModules(
    const std::vector<debug_ipc::Module>& modules) {
  TargetImpl* target = process_->target();

  // This function must be careful not to delete any references to old modules
  // that might be re-used in the new set. This variable keeps them in scope
  // while we generate the new map.
  auto old_modules = std::move(modules_);

  SystemImpl* system = target->system();
  for (const debug_ipc::Module& module : modules) {
    ModuleInfo info;
    info.name = module.name;
    info.build_id = module.build_id;
    info.base = module.base;

    Err err = system->symbols().GetModule(
        module.name, module.build_id, &info.symbols);
    if (err.has_error()) {
      if (!ExpectSymbolsForName(module.name))
        process_->NotifyOnSymbolLoadFailure(err);
    }
    modules_.emplace(std::piecewise_construct,
                     std::forward_as_tuple(module.base),
                     std::forward_as_tuple(std::move(info)));
  }

  // Update the TargetSymbols last. It may have been keeping an old
  // ModuleSymbols object alive that was needed above.
  target->symbols()->RemoveAllModules();
  for (auto& pair : modules_) {
    if (pair.second.symbols)
      target->symbols()->AddModule(pair.second.symbols);
  }
}

std::vector<ProcessSymbols::ModuleStatus> ProcessSymbolsImpl::GetStatus() const {
  std::vector<ModuleStatus> result;
  for (const auto& pair : modules_) {
    ModuleStatus status;
    status.name = pair.second.name;
    status.build_id = pair.second.build_id;
    status.base = pair.second.base;
    status.symbols_loaded = pair.second.symbols.get();
    if (pair.second.symbols)
      status.symbol_file = pair.second.symbols->module_symbols()->name();
    result.push_back(std::move(status));
  }
  return result;
}

Location ProcessSymbolsImpl::GetLocationForAddress(uint64_t address) const {
  const ModuleInfo* info = InfoForAddress(address);
  if (!info || !info->symbols)
    return Location(Location::State::kSymbolized, address);  // Can't symbolize.

  // ModuleSymbols handles addresses relative to its base.
  Location result = info->symbols->module_symbols()->LocationForAddress(
      address - info->base);
  result.AddAddressOffset(info->base);
  return result;
}

std::vector<uint64_t> ProcessSymbolsImpl::GetAddressesForFunction(
    const std::string& name) const {
  std::vector<uint64_t> result;

  for (const auto& pair : modules_) {
    for (auto local_addr :
         pair.second.symbols->module_symbols()->AddressesForFunction(name)) {
      // Offset address for module load address.
      result.push_back(pair.first + local_addr);
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
