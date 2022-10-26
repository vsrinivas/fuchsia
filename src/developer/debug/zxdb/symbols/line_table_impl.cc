// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/line_table_impl.h"

#include <lib/syslog/cpp/macros.h>

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "src/developer/debug/zxdb/common/file_util.h"

namespace zxdb {

LineTableImpl::LineTableImpl(fxl::WeakPtr<DwarfUnit> unit,
                             const llvm::DWARFDebugLine::LineTable* line_table)
    : unit_(std::move(unit)), line_table_(line_table) {}

LineTableImpl::~LineTableImpl() = default;

size_t LineTableImpl::GetNumFileNames() const {
  if (!is_valid())
    return 0;
  return line_table_->Prologue.FileNames.size();
}

const std::vector<llvm::DWARFDebugLine::Row>& LineTableImpl::GetRows() const {
  if (!is_valid()) {
    const static std::vector<llvm::DWARFDebugLine::Row> kEmptyRows;
    return kEmptyRows;
  }
  return line_table_->Rows;
}

std::optional<std::string> LineTableImpl::GetFileNameByIndex(uint64_t file_id) const {
  if (!is_valid()) {
    // In the null case GetNumFileNames() will return 0 and the caller should have checked the
    // index was in range.
    FX_NOTREACHED();
    return std::nullopt;
  }

  std::string result;
  if (line_table_->getFileNameByIndex(
          file_id, "", llvm::DILineInfoSpecifier::FileLineInfoKind::RelativeFilePath, result)) {
    return std::optional<std::string>(NormalizePath(result));
  }
  return std::nullopt;
}

uint64_t LineTableImpl::GetFunctionDieOffsetForRow(const llvm::DWARFDebugLine::Row& row) const {
  if (!unit_)
    return 0;
  return unit_->FunctionDieOffsetForRelativeAddress(row.Address.Address);
}

}  // namespace zxdb
