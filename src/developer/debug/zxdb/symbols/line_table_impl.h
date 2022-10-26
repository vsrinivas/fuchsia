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

  // The passed-in line table pointer must outlive this class.
  LineTableImpl(fxl::WeakPtr<DwarfUnit> unit, const llvm::DWARFDebugLine::LineTable* line_table);

  ~LineTableImpl() override;

  // LineTable public implementation.
  size_t GetNumFileNames() const override;
  std::optional<std::string> GetFileNameByIndex(uint64_t file_id) const override;
  uint64_t GetFunctionDieOffsetForRow(const llvm::DWARFDebugLine::Row& row) const override;

 protected:
  // LineTable protected implementation.
  const std::vector<llvm::DWARFDebugLine::Row>& GetRows() const override;

 private:
  bool is_valid() const { return unit_ && line_table_; }

  // Possibly null.
  fxl::WeakPtr<DwarfUnit> unit_;

  // This will be null if the unit has no line table or if default constructed. If the unit_ is
  // null, this pointer should be considered invalid since the memory will be backed by that object.
  //
  // Use is_valid() to check.
  const llvm::DWARFDebugLine::LineTable* line_table_ = nullptr;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_TABLE_IMPL_H_
