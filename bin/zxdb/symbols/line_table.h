// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"

namespace zxdb {

// This virtual interface wraps the line information for a single DWARFUnit.
// This indirection allows the operations that operate on the line table to
// be more easily mocked for tests (our requirements are quite low).
class LineTable {
 public:
  virtual ~LineTable() = default;

  // Returns the number of file names referenced by this line table. The
  // DWARFDebugLine::Row::File entriers are 1-based (!) indices into a table of
  // this size.
  virtual size_t GetNumFileNames() const = 0;

  // Returns the line table row information.
  virtual const std::vector<llvm::DWARFDebugLine::Row>& GetRows() const = 0;

  // Returns the absolute file name for the given file index. This is the value
  // from DWARFDebugLine::Row::File (1-based). It will return an empty optional
  // on failure.
  virtual std::optional<std::string> GetFileNameByIndex(
      uint64_t file_id) const = 0;
};

}  // namespace zxdb
