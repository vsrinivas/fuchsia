// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/find_line.h"

#include "src/lib/fxl/logging.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "src/developer/debug/zxdb/symbols/line_table.h"

namespace zxdb {

namespace {

enum class FileChecked { kUnchecked = 0, kMatch, kNoMatch };

}  // namespace

std::vector<LineMatch> GetAllLineTableMatchesInUnit(
    const LineTable& line_table, const std::string& full_path, int line) {
  std::vector<LineMatch> result;

  // The file table usually has a bunch of entries not referenced by the line
  // table (these are usually for declarations of things).
  std::vector<FileChecked> checked;
  checked.resize(line_table.GetNumFileNames(), FileChecked::kUnchecked);

  // Once we find a file match, assume there aren't any others so we don't need
  // to keep looking up file names.
  bool file_match_found = false;

  // The |best_line| is the line number of the smallest line in the file
  // we've found >= to the search line. The |result| contains all lines
  // we've encountered in the unit so far that match this.
  constexpr int kWorstLine = std::numeric_limits<int>::max();
  int best_line = kWorstLine;

  // Rows in the line table.
  for (const llvm::DWARFDebugLine::Row& row : line_table.GetRows()) {
    // EndSequence doesn't correspond to a line. Its purpose is to mark invalid
    // code regions (say, padding between functions). Because of the format
    // of the table, it will duplicate the line and column numbers from the
    // previous row so it looks valid, but these are meaningless. Skip these
    // rows.
    if (!row.IsStmt || row.EndSequence)
      continue;

    auto file_id = row.File;  // 1-based!
    if (file_id < 1 && file_id > checked.size())
      continue;  // Symbols are corrupt.

    auto file_index = file_id - 1;  // 0-based for indexing into array.
    if (!file_match_found && checked[file_index] == FileChecked::kUnchecked) {
      // Look up effective file name and see if it's a match.
      if (auto file_name = line_table.GetFileNameByIndex(file_id)) {
        if (full_path == *file_name) {
          file_match_found = true;
          checked[file_index] = FileChecked::kMatch;
        } else {
          checked[file_index] = FileChecked::kNoMatch;
        }
      } else {
        checked[file_index] = FileChecked::kNoMatch;
      }
    }

    if (checked[file_index] == FileChecked::kMatch) {
      int row_line = static_cast<int>(row.Line);
      if (line <= row_line) {
        // All lines >= to the line in question are possibilities.
        if (row_line < best_line) {
          // Found a new best match, clear all existing ones.
          best_line = row_line;
          result.clear();
        }
        if (row_line == best_line) {
          // Accumulate all matching results.
          auto subroutine = line_table.GetSubroutineForRow(row);
          result.emplace_back(
              row.Address, row_line,
              subroutine.isValid() ? subroutine.getOffset() : 0);
        }
      }
    }
  }

  return result;
}

std::vector<LineMatch> GetBestLineMatches(
    const std::vector<LineMatch>& matches) {
  // The lowest line is tbe "best" match because GetAllLineTableMatchesInUnit()
  // returns the next row for all pairs that cross the line in question. The
  // lowest of the "next" rows will be the closest line.
  auto min_elt_iter = std::min_element(
      matches.begin(), matches.end(),
      [](const LineMatch& a, const LineMatch& b) { return a.line < b.line; });

  // This will be populated with all matches for the line equal to the best
  // one (one line can match many addresses depending on inlining and code
  // reodering).
  //
  // We only want one per inlined function instance. One function can have a
  // line split into multiple line entries (possibly disjoint or not) and we
  // want only the first one (by address). But if the same helper is inlined
  // into many places (or even twice into the same function), we want to catch
  // all of those places.
  //
  // By indexing by the [inlined] subroutine DIE offset, we can ensure there
  // is only one match per subroutine, and resolve collisions by address.
  std::map<uint32_t, size_t> die_to_match_index;
  for (size_t i = 0; i < matches.size(); i++) {
    const LineMatch& match = matches[i];
    if (match.line != min_elt_iter->line)
      continue;  // Not a match.

    auto existing = die_to_match_index.find(match.function_die_offset);
    if (existing == die_to_match_index.end()) {
      // New entry for this function.
      die_to_match_index[match.function_die_offset] = i;
    } else {
      // Duplicate in the same function, pick the lowest address.
      const LineMatch& existing_match = matches[existing->second];
      if (match.address < existing_match.address)
        die_to_match_index[match.function_die_offset] = i;  // New one better.
    }
  }

  // Convert back to a result vector.
  std::vector<LineMatch> result;
  result.reserve(die_to_match_index.size());
  for (const auto& [die, match_index] : die_to_match_index)
    result.push_back(matches[match_index]);
  return result;
}

}  // namespace zxdb
