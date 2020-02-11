// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_TABLE_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_TABLE_IMPL_H_

#include "src/developer/debug/zxdb/symbols/line_table.h"

namespace zxdb {

// Implementation of LineTable backed by LLVM's DWARFDebugLine.
class LineTableImpl : public LineTable {
 public:
  // Constructor for an empty line table.
  LineTableImpl() {}

  // The passed-in pointers must outlive this class.
  LineTableImpl(llvm::DWARFUnit* unit, const llvm::DWARFDebugLine::LineTable* line_table);

  ~LineTableImpl() override;

  // LineTable public implementation.
  size_t GetNumFileNames() const override;
  std::optional<std::string> GetFileNameByIndex(uint64_t file_id) const override;
  llvm::DWARFDie GetSubroutineForRow(const llvm::DWARFDebugLine::Row& row) const override;

 protected:
  // LineTable protected implementation.
  const std::vector<llvm::DWARFDebugLine::Row>& GetRows() const override;

 private:
  // Possibly null.
  // TODO(brettw) remove when GetSubroutineForRow() is removed (see TODO in line_table.h).
  llvm::DWARFUnit* unit_ = nullptr;

  // This will be null if the unit has no line table or if default constructred.
  const llvm::DWARFDebugLine::LineTable* line_table_ = nullptr;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_TABLE_IMPL_H_
