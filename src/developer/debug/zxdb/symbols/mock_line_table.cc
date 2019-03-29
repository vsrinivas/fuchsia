// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/mock_line_table.h"

namespace zxdb {

MockLineTable::MockLineTable(FileNameVector files, RowVector rows)
    : file_names_(std::move(files)), rows_(std::move(rows)) {}

MockLineTable::~MockLineTable() = default;

size_t MockLineTable::GetNumFileNames() const { return file_names_.size(); }

const std::vector<llvm::DWARFDebugLine::Row>& MockLineTable::GetRows() const {
  return rows_;
}

std::optional<std::string> MockLineTable::GetFileNameByIndex(
    uint64_t file_id) const {
  // File indices are 1-based!
  if (file_id == 0 || file_id > file_names_.size())
    return std::nullopt;
  return file_names_[file_id - 1];
}

llvm::DWARFDie MockLineTable::GetSubroutineForRow(
    const llvm::DWARFDebugLine::Row& row) const {
  // For now, don't support subroutine lookup in the mock.
  return llvm::DWARFDie();
}

}  // namespace zxdb
