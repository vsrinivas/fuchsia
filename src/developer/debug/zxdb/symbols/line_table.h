// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_TABLE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_TABLE_H_

#include <lib/stdcompat/span.h>

#include <optional>
#include <string>
#include <vector>

#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "src/developer/debug/zxdb/common/address_range.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/dwarf_unit.h"

namespace zxdb {

class SymbolContext;

// This virtual interface wraps the line information for a single DWARFUnit. This indirection allows
// the operations that operate on the line table to be more easily mocked for tests (our
// requirements are quite low).
class LineTable {
 public:
  using Row = llvm::DWARFDebugLine::Row;

  // The line table can contain lines marked with a line number of 0. These entries indicate code
  // that is compiler-generated and not associated with any source line.
  enum SkipMode {
    // Return the exact row entry for the address, even if it's marked compiler-generated.
    kExactMatch,

    // If a line query matches a compiler generated line, advances to the next
    // non-compiler-generated line. This usually makes more sense to the user since compiler
    // generated instructions will normally be associated with some code.
    //
    // Thie mode can still return compiler generated line entries if there is no "next" row to
    // advance to.
    kSkipCompilerGenerated,
  };

  struct FoundRow {
    FoundRow() = default;
    FoundRow(cpp20::span<const Row> s, size_t i) : sequence(s), index(i) {}

    bool empty() const { return sequence.empty(); }

    // Returns the row. Call only when !empty().
    const LineTable::Row& get() const { return sequence[index]; }

    // The sequence of rows associated with the address. These will be contiguous addresses. This
    // will be empty if nothing was matched. If nonempty, the last row will always be marked with an
    // EndSequence bit.
    cpp20::span<const Row> sequence;

    // Index within the sequence of the found row. Valid when !empty().
    size_t index = 0;
  };

  virtual ~LineTable() = default;

  // Returns the number of file names referenced by this line table. The DWARFDebugLine::Row::File
  // entries are 1-based (!) indices into a table of this size.
  virtual size_t GetNumFileNames() const = 0;

  // Returns the absolute file name for the given file index. This is the value from
  // DWARFDebugLine::Row::File (1-based). It will return an empty optional on failure, which
  // includes lookup errors or if the file is not set for this row (0 index).
  virtual std::optional<std::string> GetFileNameByIndex(uint64_t file_id) const = 0;

  // Helper wrapper to extract the file from a row.
  std::optional<std::string> GetFileNameForRow(const Row& row) const {
    return GetFileNameByIndex(row.File);
  }

  // Returns the DIE associated with the most specific inlined subroutine (or subprogram) for the
  // given row. This may be an invalid DIE if there is no subroutine for this code (could be
  // compiler-generated).
  virtual uint64_t GetFunctionDieOffsetForRow(const Row& row) const = 0;

  // Query for sequences. This is used for iterating through the entire line table.
  //
  // Sequences consist of a contiguous range of addresses and will be in sorted order.
  size_t GetNumSequences() const;
  cpp20::span<const Row> GetSequenceAt(size_t index) const;

  // Returns the sequence of rows (contiguous addresses ending in an EndSequence tag) containing the
  // address. The returned array will be empty if the address was not found. See GetRowForAddress().
  //
  // Watch out: the addresses in the returned rows will all be module-relative.
  cpp20::span<const Row> GetRowSequenceForAddress(const SymbolContext& address_context,
                                                  TargetPointer absolute_address) const;

  // Finds the row in the line table that covers the given address. If there is no match, the
  // returned sequence will be empty.
  //
  // Watch out: the addresses in the returned rows will all be module-relative.
  FoundRow GetRowForAddress(const SymbolContext& address_context, TargetPointer absolute_address,
                            SkipMode skip_mode = kSkipCompilerGenerated) const;

 protected:
  // Returns the line table row information.
  //
  // This will not necessarily be sorted by address and may contain stripped regions. Queries should
  // go through the sequence table.
  //
  // The implementation should ensure that the returned value never changes. This will be indexed
  // into sequences and cached.
  virtual const std::vector<Row>& GetRows() const = 0;

 public:
  // The DWARF row table will be mostly sorted by address but there will be sequences of addresses
  // that are out-of-order relative to each other. In pracice, on common reason for this is when
  // code is stripped, the stripped code will have its start address set back to 0.
  //
  // This tracks the blocks of rows with contiguous addresses. To find a row corresponding to an
  // address, binary search to find the block, then binary search the rows referenced by the block.
  struct Sequence {
    Sequence() = default;
    Sequence(const AddressRange& a, size_t r_begin, size_t r_end)
        : addresses(a), row_begin(r_begin), row_end(r_end) {}

    AddressRange addresses;

    // Index into GetRows() of the beginning.
    size_t row_begin = 0;

    // Index into GetRows() of the ending. This will be the index of the EndSequence row.
    //
    // If the table doesn't end in an EndSequence row, the last sequence will be ignored so this row
    // is guaranteed to exist.
    size_t row_end = 0;
  };

  // Will return an null pointer if there isn't one found.
  const Sequence* GetSequenceForRelativeAddress(TargetPointer relative_address) const;

  // Ensures that the sequences_ vector is populated from the rows.
  void EnsureSequences() const;

  // Sorted by Sequence.addresses.end() so lower_bound() can find the right one. Lazily populated,
  // see EnsureSequences().
  mutable std::vector<Sequence> sequences_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_TABLE_H_
