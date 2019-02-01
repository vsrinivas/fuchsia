// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace llvm {
class DWARFUnit;
}

namespace zxdb {

class LineTable;
class Location;
class ModuleSymbolIndex;
struct ResolveOptions;
class SymbolContext;

struct LineMatch {
  LineMatch() = default;
  LineMatch(uint64_t addr, int ln, uint32_t func)
      : address(addr), line(ln), function_die_offset(func) {}

  bool operator==(const LineMatch& other) const {
    return std::tie(address, line, function_die_offset) ==
           std::tie(other.address, other.line, other.function_die_offset);
  }

  uint64_t address = 0;
  int line = 0;

  // Absolute offset of the DIE containing the most specified inlined
  // subroutine for this address or 0 if there is no function for it. This is
  // used so we don't accidentally treat duplicate line entries in different
  // functions as the same.
  uint32_t function_die_offset = 0;
};

// Searches the given line table for the given file/line. Finds the smallest
// line greater than or equal to the input line and returns all instances
// of that line.
std::vector<LineMatch> GetAllLineTableMatchesInUnit(
    const LineTable& line_table, const std::string& full_path, int line);

// Filters the set of matches to get all instances of the closest match for the
// line, with a maximum of one per function. It's assumed that the LineMatches
// are all for the same file.
//
// LineMatches are only generated for lines that cross the line in
// question, so the closest LineMatch for this function is the one with the
// smallest line number.
//
// The "one per function" rule is because a line can often get broken into
// muliple line table entries (sometimes disjoint, sometimes not), and when
// asking for a line we want the one with the lowest address.
std::vector<LineMatch> GetBestLineMatches(
    const std::vector<LineMatch>& matches);

}  // namespace zxdb
