// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/loaded_module_symbols_impl.h"

#include "garnet/bin/zxdb/client/symbols/file_line.h"
#include "garnet/bin/zxdb/client/symbols/line_details.h"
#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"

namespace zxdb {

LoadedModuleSymbolsImpl::LoadedModuleSymbolsImpl(
    fxl::RefPtr<SystemSymbols::ModuleRef> module, uint64_t load_address)
    : module_(std::move(module)), load_address_(load_address) {}
LoadedModuleSymbolsImpl::~LoadedModuleSymbolsImpl() = default;

ModuleSymbols* LoadedModuleSymbolsImpl::GetModuleSymbols() {
  return module_->module_symbols();
}

uint64_t LoadedModuleSymbolsImpl::GetLoadAddress() const {
  return load_address_;
}

Location LoadedModuleSymbolsImpl::LocationForAddress(uint64_t address) const {
  Location location =
      module_->module_symbols()->RelativeLocationForRelativeAddress(
          AbsoluteToRelative(address));
  location.AddAddressOffset(load_address_);
  return location;
}

LineDetails LoadedModuleSymbolsImpl::LineDetailsForAddress(
    uint64_t address) const {
  LineDetails result = module_->module_symbols()->LineDetailsForRelativeAddress(
      AbsoluteToRelative(address));
  for (auto& entry : result.entries()) {
    if (!entry.range.empty()) {
      entry.range = AddressRange(RelativeToAbsolute(entry.range.begin()),
                                 RelativeToAbsolute(entry.range.end()));
    }
  }
  return result;
}

std::vector<uint64_t> LoadedModuleSymbolsImpl::AddressesForFunction(
    const std::string& name) const {
  auto result = module_->module_symbols()->RelativeAddressesForFunction(name);
  for (uint64_t& address : result)
    address = RelativeToAbsolute(address);
  return result;
}

uint64_t LoadedModuleSymbolsImpl::RelativeToAbsolute(
    uint64_t relative_address) const {
  return load_address_ + relative_address;
}

uint64_t LoadedModuleSymbolsImpl::AbsoluteToRelative(
    uint64_t absolute_address) const {
  FXL_DCHECK(absolute_address >= load_address_);
  return absolute_address - load_address_;
}

std::vector<uint64_t> LoadedModuleSymbolsImpl::AddressesForLine(
    const FileLine& line) const {
  auto result = module_->module_symbols()->RelativeAddressesForLine(line);
  for (uint64_t& address : result)
    address = RelativeToAbsolute(address);
  return result;
}

}  // namespace zxdb
