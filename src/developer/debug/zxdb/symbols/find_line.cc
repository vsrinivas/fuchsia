// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/find_line.h"

#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>

#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/line_table.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

namespace {

enum class FileChecked { kUnchecked = 0, kMatch, kNoMatch };

}  // namespace

std::vector<LineMatch> GetAllLineTableMatchesInUnit(const LineTable& line_table,
                                                    const std::string& full_path, int line) {
  std::vector<LineMatch> result;

  // The file table usually has a bunch of entries not referenced by the line table (these are
  // usually for declarations of things).
  //
  // The extra "+1" is required because the file name indices count from 1. The 0-index file name
  // index implicitly takes the file name from the compilation unit.
  std::vector<FileChecked> checked;
  checked.resize(line_table.GetNumFileNames() + 1, FileChecked::kUnchecked);

  // The |best_line| is the line number of the smallest line in the file we've found >= to the
  // search line. The |result| contains all lines we've encountered in the unit so far that match
  // this.
  constexpr int kWorstLine = std::numeric_limits<int>::max();
  int best_line = kWorstLine;

  // Rows in the line table.
  size_t num_sequences = line_table.GetNumSequences();
  for (size_t sequence_i = 0; sequence_i < num_sequences; sequence_i++) {
    cpp20::span<const llvm::DWARFDebugLine::Row> sequence = line_table.GetSequenceAt(sequence_i);

    for (const auto& row : sequence) {
      if (!row.IsStmt || row.EndSequence)
        continue;

      auto file_id = row.File;
      if (file_id >= checked.size())
        continue;  // Symbols are corrupt.

      // Note: sometimes the same file can be encoded multiple times or in different ways in the
      // same line table, so don't assume just because we found it that no other files match.
      if (checked[file_id] == FileChecked::kUnchecked) {
        // Look up effective file name and see if it's a match.
        if (auto file_name = line_table.GetFileNameByIndex(file_id)) {
          if (full_path == *file_name) {
            checked[file_id] = FileChecked::kMatch;
          } else {
            checked[file_id] = FileChecked::kNoMatch;
          }
        } else {
          checked[file_id] = FileChecked::kNoMatch;
        }
      }

      if (checked[file_id] == FileChecked::kMatch) {
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
            result.emplace_back(row.Address.Address, row_line,
                                subroutine.isValid() ? subroutine.getOffset() : 0);
          }
        }
      }
    }
  }

  return result;
}

void AppendLineMatchesForInlineCalls(const CodeBlock* block, const std::string& full_path, int line,
                                     uint64_t function_die_offset,
                                     std::vector<LineMatch>* accumulator) {
  std::vector<LineMatch> result;

  for (const LazySymbol& child : block->inner_blocks()) {
    const CodeBlock* child_block = child.Get()->As<CodeBlock>();
    if (!child_block)
      continue;  // Shouldn't happen, maybe corrupt?

    if (const Function* child_fn = child_block->As<Function>()) {
      if (child_fn->is_inline() && child_fn->call_line().file() == full_path &&
          child_fn->call_line().line() >= line) {
        // Found a potential match.
        const AddressRange& addr_range = child_fn->code_ranges().GetExtent();
        if (addr_range.size() > 0) {
          // Some inlined functions may be optimized away, only add those with code.
          accumulator->emplace_back(addr_range.begin(), child_fn->call_line().line(),
                                    function_die_offset);
        }
      }
    } else {
      // Recurse into all child code blocks. We don't need to recurse into inline functions (handled
      // above) because the toplevel call to AppendLineMatchesForInlineCalls() will be per-function
      // (counting inlines as functions for the purposes of uniquifying matches).
      AppendLineMatchesForInlineCalls(child_block, full_path, line, function_die_offset,
                                      accumulator);
    }
  }
}

std::vector<LineMatch> GetBestLineMatches(const std::vector<LineMatch>& matches) {
  // The lowest line is tbe "best" match because GetAllLineTableMatchesInUnit()
  // returns the next row for all pairs that cross the line in question. The
  // lowest of the "next" rows will be the closest line.
  auto min_elt_iter =
      std::min_element(matches.begin(), matches.end(),
                       [](const LineMatch& a, const LineMatch& b) { return a.line < b.line; });

  // This will be populated with all matches for the line equal to the best one (one line can match
  // many addresses depending on inlining and code reodering).
  //
  // We only want one per inlined function instance. One function can have a line split into
  // multiple line entries (possibly disjoint or not) and we want only the first one (by address).
  // But if the same helper is inlined into many places (or even twice into the same function), we
  // want to catch all of those places.
  //
  // By indexing by the [inlined] subroutine DIE offset, we can ensure there is only one match per
  // subroutine, and resolve collisions by address.
  std::map<uint64_t, size_t> die_to_match_index;
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

size_t GetFunctionPrologueSize(const LineTable& line_table, const Function* function) {
  const AddressRanges& code_ranges = function->code_ranges();
  if (code_ranges.empty())
    return 0;
  uint64_t code_range_begin = code_ranges.front().begin();

  // The function and line table are all defined in terms of relative addresses.
  SymbolContext rel_context = SymbolContext::ForRelativeAddresses();

  LineTable::FoundRow found = line_table.GetRowForAddress(rel_context, code_range_begin);
  if (found.empty())
    return 0;
  size_t first_row = found.index;

  // Give up after this many line table entries. If prologue_end isn't found by then, assume there's
  // no specifically marked prologue. Normally it will be the 2nd entry.
  constexpr size_t kMaxSearchCount = 4;

  // Search for a line in the function with |prologue_end| explicitly marked.
  size_t prologue_end_index = first_row;
  bool found_marked_end = false;
  for (size_t i = 0; i < kMaxSearchCount && first_row + i < found.sequence.size(); i++) {
    if (!code_ranges.InRange(found.sequence[first_row + i].Address.Address))
      break;  // Outside the function.

    if (found.sequence[first_row + i].PrologueEnd) {
      // Found match.
      prologue_end_index = first_row + i;
      found_marked_end = true;
      break;
    }
  }

  if (!found_marked_end) {
    // GCC doesn't seem to generate prologue_end annotations in many cases. There, the first line
    // table entry row is interpreted as the prologue so the end is the following one.
    if (prologue_end_index < found.sequence.size() - 1)
      prologue_end_index++;
  }

  // There can be compiler-generated code immediately following the prologue annotated by "line 0".
  // Count this as prologue also.
  while (prologue_end_index < found.sequence.size() && found.sequence[prologue_end_index].Line == 0)
    prologue_end_index++;

  // Sanity check: None of those previous operations should have left us outside of the function's
  // code or outside of a known instruction (there's an end_sequence marker). If it did, this line
  // table looks different than we expect and we don't report a prologue.
  if (!code_ranges.InRange(found.sequence[prologue_end_index].Address.Address) ||
      found.sequence[prologue_end_index].EndSequence)
    return 0;

  return found.sequence[prologue_end_index].Address.Address - code_range_begin;
}

}  // namespace zxdb
