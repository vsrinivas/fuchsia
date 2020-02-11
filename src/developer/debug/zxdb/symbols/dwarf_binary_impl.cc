// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_binary_impl.h"

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/symbols/dwarf_unit_impl.h"
#include "src/lib/elflib/elflib.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

DwarfBinaryImpl::DwarfBinaryImpl(const std::string& name, const std::string& binary_name,
                                 const std::string& build_id)
    : name_(name), binary_name_(binary_name), build_id_(build_id), weak_factory_(this) {}

DwarfBinaryImpl::~DwarfBinaryImpl() {}

fxl::WeakPtr<DwarfBinaryImpl> DwarfBinaryImpl::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

std::string DwarfBinaryImpl::GetName() const { return name_; }

std::string DwarfBinaryImpl::GetBuildID() const { return build_id_; }

std::time_t DwarfBinaryImpl::GetModificationTime() const { return modification_time_; }

bool DwarfBinaryImpl::HasBinary() const {
  if (!binary_name_.empty())
    return true;

  if (auto debug = elflib::ElfLib::Create(name_))
    return debug->ProbeHasProgramBits();

  return false;
}

Err DwarfBinaryImpl::Load() {
  if (auto debug = elflib::ElfLib::Create(name_)) {
    if (debug->ProbeHasProgramBits()) {
      // Found in ".debug" file.
      plt_symbols_ = debug->GetPLTOffsets();
      if (const auto opt_syms = debug->GetAllSymbols())
        elf_symbols_ = std::move(*opt_syms);
    } else if (auto elf = elflib::ElfLib::Create(binary_name_)) {
      // Found in binary file.
      plt_symbols_ = elf->GetPLTOffsets();
      if (const auto opt_syms = elf->GetAllSymbols())
        elf_symbols_ = std::move(*opt_syms);
    }
  }

  llvm::Expected<llvm::object::OwningBinary<llvm::object::Binary>> bin_or_err =
      llvm::object::createBinary(name_);
  if (!bin_or_err) {
    auto err_str = llvm::toString(bin_or_err.takeError());
    return Err("Error loading symbols for \"" + name_ + "\": " + err_str);
  }

  modification_time_ = GetFileModificationTime(name_);

  auto binary_pair = bin_or_err->takeBinary();
  binary_buffer_ = std::move(binary_pair.second);
  binary_ = std::move(binary_pair.first);

  context_ =
      llvm::DWARFContext::create(*object_file(), nullptr, llvm::DWARFContext::defaultErrorHandler);
  context_->getDWARFObj().forEachInfoSections([this](const llvm::DWARFSection& s) {
    compile_units_.addUnitsForSection(*context_, s, llvm::DW_SECT_INFO);
  });

  return Err();
}

llvm::object::ObjectFile* DwarfBinaryImpl::GetLLVMObjectFile() { return object_file(); }

llvm::DWARFContext* DwarfBinaryImpl::GetLLVMContext() { return context(); }

const std::map<std::string, llvm::ELF::Elf64_Sym>& DwarfBinaryImpl::GetELFSymbols() const {
  return elf_symbols_;
}

const std::map<std::string, uint64_t> DwarfBinaryImpl::GetPLTSymbols() const {
  return plt_symbols_;
}

size_t DwarfBinaryImpl::GetUnitCount() const {
  auto unit_range = context_->normal_units();
  return unit_range.end() - unit_range.begin();
}

fxl::RefPtr<DwarfUnit> DwarfBinaryImpl::GetUnitAtIndex(size_t i) {
  FXL_DCHECK(i < GetUnitCount());
  return FromLLVMUnit(context_->getUnitAtIndex(i));
}

fxl::RefPtr<DwarfUnit> DwarfBinaryImpl::UnitForRelativeAddress(uint64_t relative_address) {
  return FromLLVMUnit(
      compile_units_.getUnitForOffset(context_->getDebugAranges()->findAddress(relative_address)));
}

fxl::RefPtr<DwarfUnit> DwarfBinaryImpl::FromLLVMUnit(llvm::DWARFUnit* llvm_unit) {
  if (!llvm_unit)
    return fxl::RefPtr<DwarfUnit>();

  auto found = unit_map_.find(llvm_unit);
  if (found == unit_map_.end()) {
    auto unit = fxl::MakeRefCounted<DwarfUnitImpl>(this, llvm_unit);
    unit_map_[llvm_unit] = unit;
    return unit;
  }
  return found->second;
}

}  // namespace zxdb
