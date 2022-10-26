// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_unit_impl.h"

#include <lib/syslog/cpp/macros.h>

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/symbols/dwarf_binary_impl.h"
#include "src/developer/debug/zxdb/symbols/line_table_impl.h"

namespace zxdb {

DwarfUnitImpl::DwarfUnitImpl(DwarfBinaryImpl* binary, llvm::DWARFUnit* unit)
    : binary_(binary->GetWeakPtr()), unit_(unit) {}

uint64_t DwarfUnitImpl::FunctionDieOffsetForRelativeAddress(uint64_t relative_address) const {
  if (!binary_)
    return 0;

  llvm::DWARFDie die = unit_->getSubroutineForAddress(relative_address);
  if (!die.isValid())
    return 0;
  return die.getOffset();
}

uint64_t DwarfUnitImpl::GetOffset() const {
  if (!binary_)
    return 0;
  return unit_->getOffset();
}

std::string DwarfUnitImpl::GetCompilationDir() const {
  if (!binary_)
    return std::string();

  // getCompilationDir() can return null if unset so be careful not to crash.
  const char* comp_dir = unit_->getCompilationDir();
  if (!comp_dir)
    return std::string();
  return std::string(comp_dir);
}

const LineTable& DwarfUnitImpl::GetLineTable() const {
  if (!line_table_) {
    if (binary_) {
      line_table_.emplace(GetWeakPtr(), GetLLVMLineTable());
    } else {
      line_table_.emplace();
    }
  }
  return *line_table_;
}

const llvm::DWARFDebugLine::LineTable* DwarfUnitImpl::GetLLVMLineTable() const {
  if (!binary_)
    return nullptr;
  return binary_->context()->getLineTableForUnit(unit_);
}

}  // namespace zxdb
