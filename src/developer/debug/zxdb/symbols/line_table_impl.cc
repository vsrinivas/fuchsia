// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/line_table_impl.h"

#include "llvm/DebugInfo/DWARF/DWARFContext.h"

namespace zxdb {

LineTableImpl::LineTableImpl(llvm::DWARFContext* context, llvm::DWARFUnit* unit)
    : unit_(unit),
      compilation_dir_(unit->getCompilationDir()),
      line_table_(context->getLineTableForUnit(unit)) {}

LineTableImpl::~LineTableImpl() = default;

size_t LineTableImpl::GetNumFileNames() const {
  return line_table_->Prologue.FileNames.size();
}

const std::vector<llvm::DWARFDebugLine::Row>& LineTableImpl::GetRows() const {
  return line_table_->Rows;
}

std::optional<std::string> LineTableImpl::GetFileNameByIndex(
    uint64_t file_id) const {
  std::string result;
  if (line_table_->getFileNameByIndex(
          file_id, compilation_dir_,
          llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
          result))
    return std::optional<std::string>(std::move(result));
  return std::nullopt;
}

llvm::DWARFDie LineTableImpl::GetSubroutineForRow(
    const llvm::DWARFDebugLine::Row& row) const {
  return unit_->getSubroutineForAddress(row.Address);
}

}  // namespace zxdb
