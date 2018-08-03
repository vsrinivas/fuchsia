// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <iosfwd>
#include <vector>

#include "garnet/bin/zxdb/client/symbols/file_line.h"
#include "garnet/bin/zxdb/common/address_range.h"

namespace zxdb {

// Detailed source information for a given location.
//
// TODO(brettw) this will need inline subroutine information as well.
class LineDetails {
 public:
  struct LineEntry {
    int column = 0;  // 1-based, but 0 indicates whole line.
    AddressRange range;
  };

  LineDetails();
  LineDetails(FileLine fl);
  ~LineDetails();

  const FileLine& file_line() const { return file_line_; }

  const std::vector<LineEntry>& entries() const { return entries_; }
  std::vector<LineEntry>& entries() { return entries_; }

  // For debugging, writes this to a stream.
  void Dump(std::ostream& out) const;

 private:
  FileLine file_line_;
  std::vector<LineEntry> entries_;
};

}  // namespace zxdb
