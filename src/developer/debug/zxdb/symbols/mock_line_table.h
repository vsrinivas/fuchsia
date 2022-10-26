// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_LINE_TABLE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_LINE_TABLE_H_

#include "src/developer/debug/zxdb/symbols/line_table.h"

namespace zxdb {

class MockLineTable : public LineTable {
 public:
  // Note: The file name table uses a 0-based index, while the "File" member of the row table is
  // 1-based.
  using FileNameVector = std::vector<std::string>;
  using RowVector = std::vector<llvm::DWARFDebugLine::Row>;

  MockLineTable(FileNameVector files, RowVector rows);
  ~MockLineTable() override;

  // LineTable implementation.
  size_t GetNumFileNames() const override;
  const std::vector<llvm::DWARFDebugLine::Row>& GetRows() const override;
  std::optional<std::string> GetFileNameByIndex(uint64_t file_id) const override;
  uint64_t GetFunctionDieOffsetForRow(const llvm::DWARFDebugLine::Row& row) const override;

  // Helper to construct a line table row.
  //
  // Note that the |file| is a 1-based number (subtract 1 to index into file_names_).
  //
  // All flags will be set to 0 except:
  //   - is_statement by MakeStatementRow(), MakePrologueEndRow() and MakeEndSequenceRow().
  //   - prologue_end by MakePrologueEndRow().
  //   - end_sequence by MakeEndSequenceRow().
  static Row MakeStatementRow(uint64_t address, uint16_t file, uint32_t line);
  static Row MakeNonStatementRow(uint64_t address, uint16_t file, uint32_t line);
  static Row MakePrologueEndRow(uint64_t address, uint16_t file, uint32_t line);
  static Row MakeEndSequenceRow(uint64_t address, uint16_t file, uint32_t line);

 private:
  FileNameVector file_names_;
  RowVector rows_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_LINE_TABLE_H_
