// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/module_symbols_impl.h"

#include <algorithm>

#include "garnet/bin/zxdb/symbols/dwarf_expr_eval.h"
#include "garnet/bin/zxdb/symbols/dwarf_symbol_factory.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/input_location.h"
#include "garnet/bin/zxdb/symbols/line_details.h"
#include "garnet/bin/zxdb/symbols/resolve_options.h"
#include "garnet/bin/zxdb/symbols/symbol_context.h"
#include "garnet/bin/zxdb/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/variable.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"

namespace zxdb {

namespace {

enum class FileChecked { kUnchecked = 0, kMatch, kNoMatch };

// Implementation of SymbolDataProvider that returns no memory or registers.
// This is used when evaluating global variables' location expressions which
// normally just declare an address. See LocationForVariable().
class GlobalSymbolDataProvider : public SymbolDataProvider {
 public:
  static Err GetContextError() {
    return Err(
        "Global variable requires register or memory data to locate. "
        "Please file a bug with a repro.");
  }

  // SymbolDataProvider implementation.
  std::optional<uint64_t> GetRegister(int dwarf_register_number) override {
    return std::nullopt;
  }
  void GetRegisterAsync(int dwarf_register_number,
                        GetRegisterCallback callback) override {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(callback)]() { cb(GetContextError(), 0); });
  }
  std::optional<uint64_t> GetFrameBase() override { return std::nullopt; }
  void GetFrameBaseAsync(GetRegisterCallback callback) override {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(callback)]() { cb(GetContextError(), 0); });
  }
  void GetMemoryAsync(uint64_t address, uint32_t size,
                      GetMemoryCallback callback) override {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(callback)]() {
          cb(GetContextError(), std::vector<uint8_t>());
        });
  }
};

bool SameFileLine(const llvm::DWARFDebugLine::Row& a,
                  const llvm::DWARFDebugLine::Row& b) {
  return a.File == b.File && a.Line == b.Line;
}

struct LineMatch {
  uint64_t address = 0;
  const llvm::DWARFUnit* unit = 0;
  int line = 0;

  // Absolute offset of the DIE containing the function for this address or 0
  // if there is no function for it.
  uint32_t function_die_offset = 0;
};

std::vector<LineMatch> GetBestLineTableMatchesInUnit(
    llvm::DWARFContext* context, llvm::DWARFUnit* unit,
    const std::string& full_path, int line) {
  std::vector<LineMatch> result;

  const llvm::DWARFDebugLine::LineTable* line_table =
      context->getLineTableForUnit(unit);
  const char* compilation_dir = unit->getCompilationDir();

  // The file table usually has a bunch of entries not referenced by the line
  // table (these are usually for declarations of things).
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

  compile_units_.addUnitsForSection(
      *context_, context_->getDWARFObj().getInfoSection(), llvm::DW_SECT_INFO);

  // We could consider creating a new binary/object file just for indexing.
  // The indexing will page all of the binary in, and most of it won't be
  // needed again (it will be paged back in slowly, savings may make
  // such a change worth it for large programs as needed).
  //
  // Although it will be slightly slower to create, the memory savings may make
  // such a change worth it for large programs.
  index_.CreateIndex(obj);
  return Err();
}

std::vector<Location> ModuleSymbolsImpl::ResolveInputLocation(
    const SymbolContext& symbol_context, const InputLocation& input_location,
    const ResolveOptions& options) const {
  switch (input_location.type) {
    case InputLocation::Type::kNone:
      return std::vector<Location>();
    case InputLocation::Type::kLine:
      return ResolveLineInputLocation(symbol_context, input_location, options);
    case InputLocation::Type::kSymbol:
      return ResolveSymbolInputLocation(symbol_context, input_location,
                                        options);
    case InputLocation::Type::kAddress:
      return ResolveAddressInputLocation(symbol_context, input_location,
                                         options);
  }
}

LineDetails ModuleSymbolsImpl::LineDetailsForAddress(
    const SymbolContext& symbol_context, uint64_t absolute_address) const {
  uint64_t relative_address =
      symbol_context.AbsoluteToRelative(absolute_address);

  llvm::DWARFCompileUnit* unit = llvm::dyn_cast_or_null<llvm::DWARFCompileUnit>(
      CompileUnitForRelativeAddress(relative_address));
  if (!unit)
    return LineDetails();
  const llvm::DWARFDebugLine::LineTable* line_table =
      context_->getLineTableForUnit(unit);
  if (!line_table && line_table->Rows.empty())
    return LineDetails();

  const auto& rows = line_table->Rows;
  uint32_t found_row_index = line_table->lookupAddress(relative_address);

  // The row could be not found or it could be in a "nop" range indicated by
  // an "end sequence" marker. For padding between functions, the compiler will
  // insert a row with this marker to indicate everything until the next
  // address isn't an instruction. With this flag, the other information on the
  // line will be irrelevant (in practice it will be the same as for the
  // previous entry).
  if (found_row_index == line_table->UnknownRowIndex ||
      rows[found_row_index].EndSequence)
    return LineDetails();

  // Adjust the beginning and end ranges greedily to include all matching
  // entries of the same line.
  uint32_t first_row_index = found_row_index;
  while (first_row_index > 0 &&
         SameFileLine(rows[found_row_index], rows[first_row_index - 1])) {
    first_row_index--;
  }
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
    entry.range =
        AddressRange(symbol_context.RelativeToAbsolute(rows[i].Address),
                     symbol_context.RelativeToAbsolute(rows[i + 1].Address));
    result.entries().push_back(entry);
  }

  return result;
}

std::vector<std::string> ModuleSymbolsImpl::FindFileMatches(
    const std::string& name) const {
  return index_.FindFileMatches(name);
}

llvm::DWARFUnit* ModuleSymbolsImpl::CompileUnitForRelativeAddress(
    uint64_t relative_address) const {
  return compile_units_.getUnitForOffset(
      context_->getDebugAranges()->findAddress(relative_address));
}

std::vector<Location> ModuleSymbolsImpl::ResolveLineInputLocation(
    const SymbolContext& symbol_context, const InputLocation& input_location,
    const ResolveOptions& options) const {
  std::vector<Location> result;
  for (const std::string& file : FindFileMatches(input_location.line.file())) {
    ResolveLineInputLocationForFile(
        symbol_context, file, input_location.line.line(), options, &result);
  }
  return result;
}

std::vector<Location> ModuleSymbolsImpl::ResolveSymbolInputLocation(
    const SymbolContext& symbol_context, const InputLocation& input_location,
    const ResolveOptions& options) const {
  std::vector<Location> result;
  for (const auto& die_ref : index_.FindExact(input_location.symbol)) {
    LazySymbol lazy_symbol =
        symbol_factory_->MakeLazy(die_ref.ToDie(context_.get()));
    const Symbol* symbol = lazy_symbol.Get();

    if (const Function* function = symbol->AsFunction()) {
      // Symbol is a function.
      if (function->code_ranges().empty())
        continue;  // No code associated with this.

      // Compute the full file/line information if requested. This recomputes
      // function DIE which is unnecessary but makes the code structure
      // simpler and ensures the results are always the same with regard to
      // how things like inlined functions are handled (if the location maps
      // to both a function and an inlined function inside of it).
      uint64_t abs_addr =
          symbol_context.RelativeToAbsolute(function->code_ranges()[0].begin());
      if (options.symbolize)
        result.push_back(LocationForAddress(symbol_context, abs_addr));
      else
        result.emplace_back(Location::State::kAddress, abs_addr);
    } else if (const Variable* variable = symbol->AsVariable()) {
      // Symbol is a variable. This will be the case for global variables and
      // file- and class-level statics. This always symbolizes since we
      // already computed the symbol.
      result.push_back(LocationForVariable(
          symbol_context,
          fxl::RefPtr<Variable>(const_cast<Variable*>(variable))));
    } else {
      // Unknown type of symbol.
      continue;
    }
  }
  return result;
}

std::vector<Location> ModuleSymbolsImpl::ResolveAddressInputLocation(
    const SymbolContext& symbol_context, const InputLocation& input_location,
    const ResolveOptions& options) const {
  std::vector<Location> result;
  if (options.symbolize) {
    result.push_back(
        LocationForAddress(symbol_context, input_location.address));
  } else {
    result.emplace_back(Location::State::kAddress, input_location.address);
  }
  return result;
}

// This function is similar to llvm::DWARFContext::getLineInfoForAddress
// but we can't use that because we want the actual DIE reference to the
// function rather than its name.
Location ModuleSymbolsImpl::LocationForAddress(
    const SymbolContext& symbol_context, uint64_t absolute_address) const {
  // TODO(DX-695) handle addresses that aren't code like global variables.
  uint64_t relative_address =
      symbol_context.AbsoluteToRelative(absolute_address);
  llvm::DWARFUnit* unit = CompileUnitForRelativeAddress(relative_address);
  if (!unit)  // No symbol
    return Location(Location::State::kSymbolized, absolute_address);

  // Get the innermost subroutine or inlined function for the address. This
  // may be empty, but still lookup the line info below in case its present.
  llvm::DWARFDie subroutine = unit->getSubroutineForAddress(relative_address);
  LazySymbol lazy_function;
  if (subroutine)
    lazy_function = symbol_factory_->MakeLazy(subroutine);

  // Get the file/line location (may fail).
  const llvm::DWARFDebugLine::LineTable* line_table =
      context_->getLineTableForUnit(unit);
  if (line_table) {
    llvm::DILineInfo line_info;
    if (line_table->getFileLineInfoForAddress(
            relative_address, unit->getCompilationDir(),
            llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
            line_info)) {
      // Line info present.
      return Location(absolute_address,
                      FileLine(std::move(line_info.FileName), line_info.Line),
                      line_info.Column, symbol_context,
                      std::move(lazy_function));
    }
  }

  // No line information.
  return Location(absolute_address, FileLine(), 0, symbol_context,
                  std::move(lazy_function));
}

Location ModuleSymbolsImpl::LocationForVariable(
    const SymbolContext& symbol_context, fxl::RefPtr<Variable> variable) const {
  // Evaluate the DWARF expression for the variable. Global and static
  // variables' locations aren't based on CPU state. In some cases like TLS
  // the location may require CPU state or may result in a constant instead
  // of an address. In these cases give up and return an "unlocated variable."
  // These can easily be evaluated by the expression system so we can still
  // print their values.

  // Need one unique location.
  if (variable->location().locations().size() != 1)
    return Location(symbol_context, LazySymbol(std::move(variable)));

  auto global_data_provider = fxl::MakeRefCounted<GlobalSymbolDataProvider>();
  DwarfExprEval eval;
  eval.Eval(global_data_provider, symbol_context,
            variable->location().locations()[0].expression,
            [](DwarfExprEval* eval, const Err& err) {});

  // Only evaluate synchronous outputs that result in a pointer.
  if (!eval.is_complete() || !eval.is_success() ||
      eval.GetResultType() != DwarfExprEval::ResultType::kPointer)
    return Location(symbol_context, LazySymbol(std::move(variable)));

  // TODO(brettw) in all of the return cases we could in the future fill in the
  // file/line of the definition of the variable. Currently Variables don't
  // provide that (even though it's usually in the DWARF symbols).
  return Location(eval.GetResult(), FileLine(), 0, symbol_context,
                  LazySymbol(std::move(variable)));
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
void ModuleSymbolsImpl::ResolveLineInputLocationForFile(
    const SymbolContext& symbol_context, const std::string& canonical_file,
    int line_number, const ResolveOptions& options,
    std::vector<Location>* output) const {
  const std::vector<unsigned>* units =
      index_.FindFileUnitIndices(canonical_file);
  if (!units)
    return;

  std::vector<LineMatch> matches;
  for (unsigned index : *units) {
    llvm::DWARFUnit* unit = context_->getUnitAtIndex(index);

    // Complication 1 above: find all matches for this line in the unit.
    std::vector<LineMatch> unit_matches = GetBestLineTableMatchesInUnit(
        context_.get(), unit, canonical_file, line_number);

    // Complication 2 above: Only want one entry for each function or inline.
    std::vector<LineMatch> per_fn = GetFirstEntryForEachInline(unit_matches);

    matches.insert(matches.end(), per_fn.begin(), per_fn.end());
  }

  if (matches.empty())
    return;

  // Complication 3 above: Get all instances of the best match only. The best
  // match is the one with the lowest line number (found matches should all be
  // bigger than the input line, so this will be the closest).
  auto min_elt_iter = std::min_element(
      matches.begin(), matches.end(),
      [](const LineMatch& a, const LineMatch& b) { return a.line < b.line; });
  for (const LineMatch& match : matches) {
    if (match.line == min_elt_iter->line) {
      // Add this entry to the output.
      uint64_t abs_addr = symbol_context.RelativeToAbsolute(match.address);
      if (options.symbolize)
        output->push_back(LocationForAddress(symbol_context, abs_addr));
      else
        output->push_back(Location(Location::State::kAddress, abs_addr));
    }
  }
}

}  // namespace zxdb
