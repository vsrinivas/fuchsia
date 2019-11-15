// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_DETAILS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_DETAILS_H_

#include <stdint.h>

#include <iosfwd>
#include <vector>

#include "src/developer/debug/zxdb/common/address_range.h"
#include "src/developer/debug/zxdb/symbols/file_line.h"

namespace zxdb {

// Detailed source information for a given location.
class LineDetails {
 public:
  struct LineEntry {
    LineEntry() = default;
    explicit LineEntry(AddressRange r) : range(r) {}
    LineEntry(int c, AddressRange r) : column(c), range(r) {}

    int column = 0;  // 1-based, but 0 indicates whole line.
    AddressRange range;
  };

  LineDetails();
  explicit LineDetails(FileLine fl);
  explicit LineDetails(FileLine fl, std::vector<LineEntry> entries);
  ~LineDetails();

  bool is_valid() const { return !entries_.empty(); }

  // Some entries will have a 0 line number. This indicates the instruction is not associated with
  // any source code. The file is not used in that case.
  const FileLine& file_line() const { return file_line_; }

  const std::vector<LineEntry>& entries() const { return entries_; }
  std::vector<LineEntry>& entries() { return entries_; }

  // Computes the full extent of this line's ranges.
  AddressRange GetExtent() const;

  // For debugging, writes this to a stream.
  void Dump(std::ostream& out) const;

 private:
  FileLine file_line_;
  std::vector<LineEntry> entries_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_LINE_DETAILS_H_
