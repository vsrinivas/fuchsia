// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_TABLE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_TABLE_H_

#include <optional>
#include <string>
#include <vector>

#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"

namespace zxdb {

class SymbolContext;

// This virtual interface wraps the line information for a single DWARFUnit. This indirection allows
// the operations that operate on the line table to be more easily mocked for tests (our
// requirements are quite low).
class LineTable {
 public:
  using Row = llvm::DWARFDebugLine::Row;

  virtual ~LineTable() = default;

  // Returns the number of file names referenced by this line table. The DWARFDebugLine::Row::File
  // entries are 1-based (!) indices into a table of this size.
  virtual size_t GetNumFileNames() const = 0;

  // Returns the line table row information.
  virtual const std::vector<Row>& GetRows() const = 0;

  // Returns the absolute file name for the given file index. This is the value from
  // DWARFDebugLine::Row::File (1-based). It will return an empty optional on failure.
  virtual std::optional<std::string> GetFileNameByIndex(uint64_t file_id) const = 0;

  // Returns the DIE associated with the subroutine for the given row. This may be an invalid DIE if
  // there is no subroutine for this code (could be compiler-generated).
  virtual llvm::DWARFDie GetSubroutineForRow(const Row& row) const = 0;

  // Computes the index of the row in the line table that covers the given address. There should be
  // only one entry per address but in case there are duplicates this returns the first one.
  //
  // Returns a null optional if there is no entry for the address. This happens if it's before the
  // beginning of the table. If it's past the end, it will be covered by the last row of the table
  // which will normally have |end_sequence| set which indicates there's no data tnere.
  //
  // Some rows other than the last can have |end_sequence| set to indicate there's no relevant
  // data starting at that address, and the other values are irrelevant (whatever was in the state
  // machine at that time). No checking is done for these rows so the caller will need to check.
  // Such rows would normally not be counted as part of the code.
  std::optional<size_t> GetFirstRowIndexForAddress(const SymbolContext& address_context,
                                                   uint64_t absolute_address) const;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_TABLE_H_
