// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"

#include <algorithm>

#include "garnet/bin/zxdb/client/symbols/dwarf_symbol_factory.h"
#include "garnet/bin/zxdb/client/symbols/line_details.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"

namespace zxdb {

namespace {

enum class FileChecked { kUnchecked = 0, kMatch, kNoMatch };

bool SameFileLine(const llvm::DWARFDebugLine::Row& a,
                  const llvm::DWARFDebugLine::Row& b) {
  return a.File == b.File && a.Line == b.Line;
}

struct LineMatch {
  uint64_t address = 0;
  const llvm::DWARFCompileUnit* unit = 0;
  int line = 0;

  // Absolute offset of the DIE containing the function for this address or 0
  // if there is no function for it.
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
      if (line_table->getFileNameByIndex(
              file_id, compilation_dir,
              llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
              file_name)) {
        if (full_path == file_name) {
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
        match.unit = unit;
        match.line = row_line;

        auto subroutine = unit->getSubroutineForAddress(row.Address);
        if (subroutine.isValid())
          match.function_die_offset = subroutine.getOffset();
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

}  // namespace

ModuleSymbolsImpl::ModuleSymbolsImpl(const std::string& name,
                                     const std::string& build_id)
    : name_(name), build_id_(build_id), weak_factory_(this) {
  symbol_factory_ = fxl::MakeRefCounted<DwarfSymbolFactory>(GetWeakPtr());
}

ModuleSymbolsImpl::~ModuleSymbolsImpl() = default;

fxl::WeakPtr<ModuleSymbolsImpl> ModuleSymbolsImpl::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

ModuleSymbolStatus ModuleSymbolsImpl::GetStatus() const {
  ModuleSymbolStatus status;
  status.build_id = build_id_;
  status.base = 0;  // We don't know this, only ProcessSymbols does.
  status.symbols_loaded = true;  // Since this instance exists at all.
  status.functions_indexed = index_.CountSymbolsIndexed();
  status.files_indexed = index_.files_indexed();
  status.symbol_file = name_;
  return status;
}

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

  // We could consider creating a new binary/object file just for indexing.
  // The indexing will page all of the binary in, and most of it won't be
  // needed again (it will be paged back in slowl savings may make
  // such a change worth it for large programs.y as needed).
  //
  // Although it will be slightly slower to create, the memory savings may make
  // such a change worth it for large programs.
  index_.CreateIndex(obj);
  return Err();
}

Location ModuleSymbolsImpl::RelativeLocationForRelativeAddress(
    uint64_t address) const {
  // Currently this just uses the main helper functions on DWARFContext that
  // retrieve the line information.
  //
  // In the future, we will have more advanced needs, like understanding the
  // local variables at a given address, and detailed information about the
  // function they're part of. For this, we'll need the nested sequence of
  // scope DIEs plus the function declaration DIE. In that case, we'll need to
  // make this more advanced and extract the information ourselves.
  llvm::DILineInfo line_info = context_->getLineInfoForAddress(
      address,
      llvm::DILineInfoSpecifier(
          llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
          llvm::DILineInfoSpecifier::FunctionNameKind::ShortName));
  if (!line_info)
    return Location(Location::State::kSymbolized, address);  // No symbol.
  // TODO(brettw) hook up LazySymbol to the location.
  return Location(address,
                  FileLine(std::move(line_info.FileName), line_info.Line),
                  line_info.Column, line_info.FunctionName);
}

// By policy this function decides that line table entries with a "0" line
// index count with the previous non-zero entry. The compiler will generate
// a row with a 0 line number to indicate an instruction range that isn't
// associated with a source line.
LineDetails ModuleSymbolsImpl::LineDetailsForRelativeAddress(
    uint64_t address) const {
  llvm::DWARFCompileUnit* unit = CompileUnitForAddress(address);
  if (!unit)
    return LineDetails();
  const llvm::DWARFDebugLine::LineTable* line_table =
      context_->getLineTableForUnit(unit);
  if (!line_table && line_table->Rows.empty())
    return LineDetails();

  const auto& rows = line_table->Rows;
  uint32_t found_row_index = line_table->lookupAddress(address);

  // The row could be not found or it could be in a "nop" range indicated by
  // an "end sequence" marker. For padding between functions, the compiler will
  // insert a row with this marker to indicate everything until the next
  // address isn't an instruction. With this flag, the other information on the
  // line will be irrelevant (in practice it will be the same as for the
  // previous entry).
  if (found_row_index == line_table->UnknownRowIndex ||
      rows[found_row_index].EndSequence)
    return LineDetails();

  // Might have landed on a "0" line (see function comment above). Back up.
  while (found_row_index > 0 && rows[found_row_index].Line == 0)
    found_row_index--;
  if (rows[found_row_index].Line == 0)
    return LineDetails();  // Nothing has a real line number, give up.

  // Back up to the first row matching the file/line of the found one for the
  // address.
  uint32_t first_row_index = found_row_index;
  while (first_row_index > 0 &&
         SameFileLine(rows[found_row_index], rows[first_row_index - 1])) {
    first_row_index--;
  }

  // Search forward for the end of the sequence. Also include entries with
  // a "0" line number as descrived above.
  uint32_t last_row_index = found_row_index;
  while (last_row_index < rows.size() - 1 &&
         SameFileLine(rows[found_row_index], rows[last_row_index + 1])) {
    last_row_index++;
  }

  // Resolve the file name.
  const char* compilation_dir = unit->getCompilationDir();
  std::string file_name;
  line_table->getFileNameByIndex(
      rows[first_row_index].File, compilation_dir,
      llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, file_name);

  LineDetails result(FileLine(file_name, rows[first_row_index].Line));

  // Add entries for each row. The last row doesn't count because it should be
  // an end_sequence marker to provide the ending size of the previous entry.
  // So never include that.
  for (uint32_t i = first_row_index; i <= last_row_index && i < rows.size() - 1;
       i++) {
    // With loop bounds we can always dereference @ i + 1.
    if (rows[i + 1].Address < rows[i].Address)
      break;  // Going backwards, corrupted so give up.

    LineDetails::LineEntry entry;
    entry.column = rows[i].Column;
    entry.range = AddressRange(rows[i].Address, rows[i + 1].Address);
    result.entries().push_back(entry);
  }

  return result;
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
  const std::vector<unsigned>* units = index_.FindFileUnitIndices(line.file());
  if (!units)
    return result;

  std::vector<LineMatch> matches;
  for (unsigned index : *units) {
    llvm::DWARFCompileUnit* unit = context_->getCompileUnitAtIndex(index);

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

llvm::DWARFCompileUnit* ModuleSymbolsImpl::CompileUnitForAddress(
    uint64_t address) const {
  return compile_units_.getUnitForOffset(
      context_->getDebugAranges()->findAddress(address));
}

}  // namespace zxdb
