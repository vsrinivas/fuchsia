// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/line_table_impl.h"

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

LineTableImpl::LineTableImpl(llvm::DWARFUnit* unit,
                             const llvm::DWARFDebugLine::LineTable* line_table)
    : unit_(unit), line_table_(line_table) {}

LineTableImpl::~LineTableImpl() = default;

size_t LineTableImpl::GetNumFileNames() const {
  if (!line_table_)
    return 0;
  return line_table_->Prologue.FileNames.size();
}

const std::vector<llvm::DWARFDebugLine::Row>& LineTableImpl::GetRows() const {
  if (!line_table_) {
    const static std::vector<llvm::DWARFDebugLine::Row> kEmptyRows;
    return kEmptyRows;
  }
  return line_table_->Rows;
}

std::optional<std::string> LineTableImpl::GetFileNameByIndex(uint64_t file_id) const {
  if (!line_table_) {
    // In the null case GetNumFileNames() will return 0 and the caller should have checked the
    // index was in range.
    FXL_NOTREACHED();
    return std::nullopt;
  }
  if (file_id == 0)
    return std::nullopt;  // File IDs are 1-based.

  std::string result;
  if (line_table_->getFileNameByIndex(
          file_id, "", llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, result))
    return std::optional<std::string>(std::move(result));
  return std::nullopt;
}

llvm::DWARFDie LineTableImpl::GetSubroutineForRow(const llvm::DWARFDebugLine::Row& row) const {
  if (!unit_)
    return llvm::DWARFDie();
  return unit_->getSubroutineForAddress(row.Address);
}

}  // namespace zxdb
