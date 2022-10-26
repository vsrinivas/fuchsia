// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/mock_line_table.h"

namespace zxdb {

MockLineTable::MockLineTable(FileNameVector files, RowVector rows)
    : file_names_(std::move(files)), rows_(std::move(rows)) {}

MockLineTable::~MockLineTable() = default;

size_t MockLineTable::GetNumFileNames() const { return file_names_.size(); }

const std::vector<llvm::DWARFDebugLine::Row>& MockLineTable::GetRows() const { return rows_; }

std::optional<std::string> MockLineTable::GetFileNameByIndex(uint64_t file_id) const {
  // File indices are 1-based!
  if (file_id == 0 || file_id > file_names_.size())
    return std::nullopt;
  return file_names_[file_id - 1];
}

uint64_t MockLineTable::GetFunctionDieOffsetForRow(const llvm::DWARFDebugLine::Row& row) const {
  // For now, don't support subroutine lookup in the mock.
  return 0;
}

// static
LineTable::Row MockLineTable::MakeStatementRow(uint64_t address, uint16_t file, uint32_t line) {
  llvm::DWARFDebugLine::Row result = MakeNonStatementRow(address, file, line);
  result.IsStmt = 1;
  return result;
}

// static
LineTable::Row MockLineTable::MakeNonStatementRow(uint64_t address, uint16_t file, uint32_t line) {
  llvm::DWARFDebugLine::Row result;
  result.Address = {address};
  result.Line = line;
  result.Column = 0;
  result.File = file;
  result.Discriminator = 0;
  result.Isa = 0;
  result.IsStmt = 0;
  result.BasicBlock = 0;
  result.EndSequence = 0;
  result.PrologueEnd = 0;
  result.EpilogueBegin = 0;

  return result;
}

// static
LineTable::Row MockLineTable::MakePrologueEndRow(uint64_t address, uint16_t file, uint32_t line) {
  llvm::DWARFDebugLine::Row result = MakeStatementRow(address, file, line);
  result.PrologueEnd = 1;
  return result;
}

// static
LineTable::Row MockLineTable::MakeEndSequenceRow(uint64_t address, uint16_t file, uint32_t line) {
  llvm::DWARFDebugLine::Row result = MakeStatementRow(address, file, line);
  result.EndSequence = 1;
  return result;
}

}  // namespace zxdb
