// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/find_line.h"

#include "garnet/bin/zxdb/symbols/line_table.h"
#include "lib/fxl/logging.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"

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

  // Once we find a match, assume there aren't any others so we don't need to
  // keep looking up file names.
  bool file_match_found = false;

  // We save every time there's a transition from a line < the one we want to a
  // line >= the one we want. This tracks the previous line we've seen in the
  // file.
  int prev_line_matching_file = -1;

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
    FXL_DCHECK(file_id >= 1 && file_id <= checked.size());
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
      // Looking for a transition across the line of interest in the file.
      // Also catch all exact matches. This will sometimes duplicate entries
      // where the line is split across multiple statements, this will get
      // filtered out later. But if a one-line function is inlined twice in a
      // row, we want to catch both instances.
      int row_line = static_cast<int>(row.Line);
      if (line == row_line ||
          (prev_line_matching_file < line && line <= row_line)) {
        LineMatch match;
        match.address = row.Address;
        match.line = row_line;

        auto subroutine = line_table.GetSubroutineForRow(row);
        if (subroutine.isValid())
          match.function_die_offset = subroutine.getOffset();
        result.push_back(match);
      }
      prev_line_matching_file = row.Line;
    }
  }

  return result;
}

std::vector<LineMatch> FilterUnitLineMatches(
    const std::vector<LineMatch>& matches) {
  // Maps absolute DIE offsets to the index into matches of the match with the
  // smallest address for this DIE.
  std::map<uint32_t, size_t> die_to_match_index;

  for (size_t i = 0; i < matches.size(); i++) {
    const LineMatch& match = matches[i];

    // Although function_die_offset may be 0 to indicate no function, looking
    // up 0 here is still valid because that will mean "code in this file with
    // no associated function".
    auto found = die_to_match_index.find(match.function_die_offset);
    if (found == die_to_match_index.end()) {
      // First one for this DIE.
      die_to_match_index[match.function_die_offset] = i;
    } else if (match.address < matches[found->second].address) {
      // New best one.
      found->second = i;
    }
  }

  // Extract the found minimum LineMatch for each DIE.
  std::vector<LineMatch> result;
  result.reserve(die_to_match_index.size());
  for (const auto& pair : die_to_match_index)
    result.push_back(matches[pair.second]);
  return result;
}

std::vector<LineMatch> GetClosestLineMatches(
    const std::vector<LineMatch>& matches) {
  std::vector<LineMatch> result;

  auto min_elt_iter = std::min_element(
      matches.begin(), matches.end(),
      [](const LineMatch& a, const LineMatch& b) { return a.line < b.line; });
  for (const LineMatch& match : matches) {
    if (match.line == min_elt_iter->line)
      result.push_back(match);
  }
  return result;
}

}  // namespace zxdb
