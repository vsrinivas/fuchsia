// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/module_symbols.h"

#include <algorithm>

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"

namespace zxdb {

ModuleSymbols::ModuleSymbols(const std::string& name) : name_(name) {}
ModuleSymbols::~ModuleSymbols() = default;

Err ModuleSymbols::Load() {
  llvm::Expected<llvm::object::OwningBinary<llvm::object::Binary>> bin_or_err =
      llvm::object::createBinary(name_);
  if (!bin_or_err) {
    auto err_str = llvm::toString(bin_or_err.takeError());
    return Err("Error loading symbols for \"" + name_ + "\": " +
               err_str);
  }

  auto binary_pair = bin_or_err->takeBinary();
  binary_buffer_ = std::move(binary_pair.second);
  binary_ = std::move(binary_pair.first);

  llvm::object::ObjectFile* obj =
      static_cast<llvm::object::ObjectFile*>(binary_.get());
  context_ = llvm::DWARFContext::create(
      *obj, nullptr, llvm::DWARFContext::defaultErrorHandler);

  compile_units_.parse(*context_, context_->getDWARFObj().getInfoSection());

  index_.CreateIndex(context_.get(), compile_units_);
  return Err();
}

Location ModuleSymbols::LocationForAddress(uint64_t address) const {
  // Currently this just uses the main helper functions on DWARFContext that
  // retrieve the line information.
  //
  // In the future, we will ahve more advanced needs, like understanding the
  // local variables at a given address, and detailed information about the
  // function they're part of. For this, we'll need the nested sequence of
  // scope DIEs plus the function declaration DIE. In that case, we'll need to
  // make this more advanced and extract the information ourselves.
  llvm::DILineInfo line_info = context_->getLineInfoForAddress(address);
  if (!line_info)
    return Location(Location::State::kSymbolized, address);  // No symbol.
  return Location(address, FileLine(line_info.FileName, line_info.Line),
                  line_info.Column);
}

std::vector<uint64_t> ModuleSymbols::AddressesForFunction(
    const std::string& name) const {
  const std::vector<llvm::DWARFDie>& entries = index_.FindFunctionExact(name);

  std::vector<uint64_t> result;
  for (const auto& cur : entries) {
    llvm::DWARFAddressRangesVector ranges = cur.getAddressRanges();
    if (ranges.empty())
      continue;

    // Get the minimum address associated with this DIE.
    auto min_iter = std::min_element(
        ranges.begin(), ranges.end(),
        [](const llvm::DWARFAddressRange& a, const llvm::DWARFAddressRange& b) {
          return a.LowPC < b.LowPC;
        });
    result.push_back(min_iter->LowPC);
  }
  return result;
}

}  // namespace zxdb
