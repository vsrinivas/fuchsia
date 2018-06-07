// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"

#include <algorithm>

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"

namespace zxdb {

namespace {

enum class FileChecked { kUnchecked = 0, kMatch, kNoMatch };

struct LineMatch {
  uint64_t address = 0;
  const llvm::DWARFCompileUnit* unit = 0;
  int line = 0;

  // Absolute offset of the DIE containing the function for this address.
  uint32_t function_die_offset = 0;
};

std::vector<LineMatch> GetBestLineTableMatchesInUnit(
    llvm::DWARFContext* context, llvm::DWARFCompileUnit* unit,
    const std::string& full_path, int line) {
  std::vector<LineMatch> result;

  const llvm::DWARFDebugLine::LineTable* line_table =
      context->getLineTableForUnit(unit);
  const char* compilation_dir = unit->getCompilationDir();

  // The file table usually has a bunch of entries not referenced by the line
  // table (these are uusually for declarations of things).
  std::vector<FileChecked> checked;
  checked.resize(line_table->Prologue.FileNames.size(),
                 FileChecked::kUnchecked);

  // Once we find a match, assume there aren't any others so we don't need to
  // keep looking up file names.
  bool file_match_found = false;

  // We save every time there's a transition from a line < the one we want to a
  // line >= the one we want. This tracks the previous line we've seen in the
  // file.
  int prev_line_matching_file = -1;

  // Rows in the line table.
  std::string file_name;
  for (size_t i = 0; i < line_table->Rows.size(); i++) {
    const llvm::DWARFDebugLine::Row& row = line_table->Rows[i];
    if (!row.IsStmt)
      continue;

    auto file_id = row.File;  // 1-based!
    FXL_DCHECK(file_id >= 1 && file_id <= checked.size());
    auto file_index = file_id - 1;  // 0-based for indexing into array.
    if (!file_match_found &&
        checked[file_index - 1] == FileChecked::kUnchecked) {
      // Look up effective file name and see if it's a match.
      if (line_table->getFileNameByIndex(
              file_id, compilation_dir,
              llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
              file_name)) {
        checked[file_index] = full_path == file_name ? FileChecked::kMatch
                                                     : FileChecked::kNoMatch;
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
        match.unit = unit;
        match.line = row_line;
        match.function_die_offset =
            unit->getSubroutineForAddress(row.Address).getOffset();
        result.push_back(match);
      }
      prev_line_matching_file = row.Line;
    }
  }

  return result;
}

// Filters the list to remove matches being in the same function or inline.
//
// We expect to have few results in the vector so vector performance should be
// good enough. Returning a new copy keeps the code a little simpler than
// mutating in place.
std::vector<LineMatch> GetFirstEntryForEachInline(
    const std::vector<LineMatch>& matches) {
  // Maps absolute DIE offsets to the index into matches of the match with the
  // smallest address for this DIE.
  std::map<uint32_t, size_t> die_to_match_index;

  for (size_t i = 0; i < matches.size(); i++) {
    const LineMatch& match = matches[i];

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

}  // namespace

ModuleSymbolsImpl::ModuleSymbolsImpl(const std::string& name) : name_(name) {}
ModuleSymbolsImpl::~ModuleSymbolsImpl() = default;

const std::string& ModuleSymbolsImpl::GetLocalFileName() const { return name_; }

Err ModuleSymbolsImpl::Load() {
  llvm::Expected<llvm::object::OwningBinary<llvm::object::Binary>> bin_or_err =
      llvm::object::createBinary(name_);
  if (!bin_or_err) {
    auto err_str = llvm::toString(bin_or_err.takeError());
    return Err("Error loading symbols for \"" + name_ + "\": " + err_str);
  }

  auto binary_pair = bin_or_err->takeBinary();
  binary_buffer_ = std::move(binary_pair.second);
  binary_ = std::move(binary_pair.first);

  llvm::object::ObjectFile* obj =
      static_cast<llvm::object::ObjectFile*>(binary_.get());
  context_ = llvm::DWARFContext::create(
      *obj, nullptr, llvm::DWARFContext::defaultErrorHandler);

  compile_units_.parse(*context_, context_->getDWARFObj().getInfoSection());

  index_.CreateIndex(context_.get(), compile_units_);
  return Err();
}

Location ModuleSymbolsImpl::RelativeLocationForRelativeAddress(
    uint64_t address) const {
  // Currently this just uses the main helper functions on DWARFContext that
  // retrieve the line information.
  //
  // In the future, we will ahve more advanced needs, like understanding the
  // local variables at a given address, and detailed information about the
  // function they're part of. For this, we'll need the nested sequence of
  // scope DIEs plus the function declaration DIE. In that case, we'll need to
  // make this more advanced and extract the information ourselves.
  llvm::DILineInfo line_info = context_->getLineInfoForAddress(address);
  if (!line_info)
    return Location(Location::State::kSymbolized, address);  // No symbol.
  return Location(address,
                  FileLine(std::move(line_info.FileName), line_info.Line),
                  line_info.Column);
}

std::vector<uint64_t> ModuleSymbolsImpl::RelativeAddressesForFunction(
    const std::string& name) const {
  const std::vector<ModuleSymbolIndexNode::DieRef>& entries =
      index_.FindFunctionExact(name);

  std::vector<uint64_t> result;
  for (const auto& cur : entries) {
    llvm::DWARFDie die = cur.ToDie(context_.get());

    llvm::DWARFAddressRangesVector ranges = die.getAddressRanges();
    if (ranges.empty())
      continue;

    // Get the minimum address associated with this DIE.
    auto min_iter = std::min_element(
        ranges.begin(), ranges.end(),
        [](const llvm::DWARFAddressRange& a, const llvm::DWARFAddressRange& b) {
          return a.LowPC < b.LowPC;
        });
    result.push_back(min_iter->LowPC);
  }
  return result;
}

std::vector<std::string> ModuleSymbolsImpl::FindFileMatches(
    const std::string& name) const {
  return index_.FindFileMatches(name);
}

// To a first approximation we just look up the line in the line table for
// each compilation unit that references the file. Complications:
//
// 1. The line might not be an exact match (the user can specify a blank line
//    or something optimized out). In this case, find the next valid line.
//
// 2. Inlining and templates can mean there are multiple matches per
//    compilation unit, and a single line can have multiple line table entries
//    even if the code isn't duplicated. Take the first match for each function
//    implementation or inlined block.
//
// 3. The above step can find many different locations. Maybe some code from
//    the file in question is inlined into the compilation unit, but not the
//    function with the line in it. Or different template instantiations can
//    mean that a line of code is in some instantiations but don't apply to
//    others.
//
//    To solve this duplication problem, get the resolved line of each of the
//    addresses found above and find the best one. Keep only those locations
//    matching the best one (there can still be multiple).
std::vector<uint64_t> ModuleSymbolsImpl::RelativeAddressesForLine(
    const FileLine& line) const {
  std::vector<uint64_t> result;
  const std::vector<llvm::DWARFCompileUnit*>* units =
      index_.FindFileUnits(line.file());
  if (!units)
    return result;

  std::vector<LineMatch> matches;
  for (llvm::DWARFCompileUnit* unit : *units) {
    // Complication 1 above: find all matches for this line in the unit.
    std::vector<LineMatch> unit_matches = GetBestLineTableMatchesInUnit(
        context_.get(), unit, line.file(), line.line());

    // Complication 2 above: Only want one entry for each function or inline.
    std::vector<LineMatch> per_fn = GetFirstEntryForEachInline(unit_matches);

    matches.insert(matches.end(), per_fn.begin(), per_fn.end());
  }

  if (matches.empty())
    return result;

  // Complication 3 above: Get all instances of the best match only. The best
  // match is the one with the lowest line number (found matches should all be
  // bigger than the input line, so this will be the closest).
  auto min_elt_iter = std::min_element(
      matches.begin(), matches.end(),
      [](const LineMatch& a, const LineMatch& b) { return a.line < b.line; });
  for (const LineMatch& match : matches) {
    if (match.line == min_elt_iter->line)
      result.push_back(match.address);
  }
  return result;
}

}  // namespace zxdb
