// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/zxdb/symbols/line_table.h"

namespace zxdb {

class MockLineTable : public LineTable {
 public:
  using FileNameVector = std::vector<std::string>;
  using RowVector = std::vector<llvm::DWARFDebugLine::Row>;

  MockLineTable(FileNameVector files, RowVector rows);
  ~MockLineTable() override;

  // LineTable implementation.
  size_t GetNumFileNames() const override;
  const std::vector<llvm::DWARFDebugLine::Row>& GetRows() const override;
  std::optional<std::string> GetFileNameByIndex(
      uint64_t file_id) const override;
  llvm::DWARFDie GetSubroutineForRow(
      const llvm::DWARFDebugLine::Row& row) const override;

 private:
  FileNameVector file_names_;
  RowVector rows_;
};

}  // namespace zxdb
