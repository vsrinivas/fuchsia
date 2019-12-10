// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"

#include <stdio.h>

#include <algorithm>

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/common/largest_less_or_equal.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"
#include "src/developer/debug/zxdb/symbols/dwarf_symbol_factory.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/find_line.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/line_table_impl.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/elflib/elflib.h"

namespace zxdb {

namespace {

// Implementation of SymbolDataProvider that returns no memory or registers. This is used when
// evaluating global variables' location expressions which normally just declare an address. See
// LocationForVariable().
class GlobalSymbolDataProvider : public SymbolDataProvider {
 public:
  static Err GetContextError() {
    return Err(
        "Global variable requires register or memory data to locate. "
        "Please file a bug with a repro.");
  }

  // SymbolDataProvider implementation.
  debug_ipc::Arch GetArch() override { return debug_ipc::Arch::kUnknown; }
  void GetRegisterAsync(debug_ipc::RegisterID, GetRegisterCallback callback) override {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(callback)]() mutable { cb(GetContextError(), {}); });
  }
  void GetFrameBaseAsync(GetFrameBaseCallback callback) override {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(callback)]() mutable { cb(GetContextError(), 0); });
  }
  void GetMemoryAsync(uint64_t address, uint32_t size, GetMemoryCallback callback) override {
    debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(callback)]() mutable {
      cb(GetContextError(), std::vector<uint8_t>());
    });
  }
  void WriteMemory(uint64_t address, std::vector<uint8_t> data, WriteCallback cb) override {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() mutable { cb(GetContextError()); });
  }
};

// The order of the parameters matters because "line 0" is handled in "greedy" mode only for the
// candidate line. If the caller is asking about an address that matches line 0, we don't want to
// expand that past line boundaries, but we do want to expand other lines actoss line 0 in greedy
// mode.
bool SameFileLine(const llvm::DWARFDebugLine::Row& reference,
                  const llvm::DWARFDebugLine::Row& candidate, bool greedy) {
  if (greedy && candidate.Line == 0)
    return true;
  return reference.File == candidate.File && reference.Line == candidate.Line;
}

// Determines if the given input location references a PLT symbol. If it does, returns the name of
// that symbol (with the "@plt" annotation stripped). If it does not, returns a null optional.
std::optional<std::string> GetPLTInputLocation(const InputLocation& loc) {
  if (loc.type != InputLocation::Type::kName || loc.name.components().size() != 1)
    return std::nullopt;

  const IdentifierComponent& comp = loc.name.components()[0];
  if (!StringEndsWith(comp.name(), "@plt"))
    return std::nullopt;

  return comp.name().substr(0, comp.name().size() - 4);
}

// Returns true if the given input references the special "main" function annotation.
bool ReferencesMainFunction(const InputLocation& loc) {
  if (loc.type != InputLocation::Type::kName || loc.name.components().size() != 1)
    return false;
  return loc.name.components()[0].name() == "@main";
}

}  // namespace

ModuleSymbolsImpl::ModuleSymbolsImpl(const std::string& name, const std::string& binary_name,
                                     const std::string& build_id)
    : name_(name), binary_name_(binary_name), build_id_(build_id), weak_factory_(this) {}

ModuleSymbolsImpl::~ModuleSymbolsImpl() = default;

fxl::WeakPtr<ModuleSymbolsImpl> ModuleSymbolsImpl::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

ModuleSymbolStatus ModuleSymbolsImpl::GetStatus() const {
  ModuleSymbolStatus status;
  status.build_id = build_id_;
  status.base = 0;               // We don't know this, only ProcessSymbols does.
  status.symbols_loaded = true;  // Since this instance exists at all.
  status.functions_indexed = index_.CountSymbolsIndexed();
  status.files_indexed = index_.files_indexed();
  status.symbol_file = name_;
  return status;
}

std::time_t ModuleSymbolsImpl::GetModificationTime() const { return modification_time_; }

Err ModuleSymbolsImpl::Load(bool create_index) {
  DEBUG_LOG(Session) << "Loading " << binary_name_ << " (" << name_ << ").";

  if (auto debug = elflib::ElfLib::Create(name_)) {
    std::map<std::string, uint64_t> plt_syms;
    std::optional<std::map<std::string, llvm::ELF::Elf64_Sym>> opt_syms;
    if (debug->ProbeHasProgramBits()) {
      // Found in ".debug" file.
      plt_syms = debug->GetPLTOffsets();
      opt_syms = debug->GetAllSymbols();
    } else if (auto elf = elflib::ElfLib::Create(binary_name_)) {
      // Found in binary file.
      plt_syms = elf->GetPLTOffsets();
      opt_syms = elf->GetAllSymbols();
    }

    FillElfSymbols(opt_syms ? *opt_syms : std::map<std::string, llvm::ELF::Elf64_Sym>(), plt_syms);
  }

  llvm::Expected<llvm::object::OwningBinary<llvm::object::Binary>> bin_or_err =
      llvm::object::createBinary(name_);
  if (!bin_or_err) {
    auto err_str = llvm::toString(bin_or_err.takeError());
    return Err("Error loading symbols for \"" + name_ + "\": " + err_str);
  }

  modification_time_ = GetFileModificationTime(name_);

  auto binary_pair = bin_or_err->takeBinary();
  binary_buffer_ = std::move(binary_pair.second);
  binary_ = std::move(binary_pair.first);

  context_ =
      llvm::DWARFContext::create(*object_file(), nullptr, llvm::DWARFContext::defaultErrorHandler);
  context_->getDWARFObj().forEachInfoSections([this](const llvm::DWARFSection& s) {
    compile_units_.addUnitsForSection(*context_, s, llvm::DW_SECT_INFO);
  });
  symbol_factory_ = fxl::MakeRefCounted<DwarfSymbolFactory>(GetWeakPtr());

  if (create_index) {
    // We could consider creating a new binary/object file just for indexing. The indexing will page
    // all of the binary in, and most of it won't be needed again (it will be paged back in slowly,
    // savings may make such a change worth it for large programs as needed).
    //
    // Although it will be slightly slower to create, the memory savings may make such a change
    // worth it for large programs.
    index_.CreateIndex(object_file());
  }

  return Err();
}

std::vector<Location> ModuleSymbolsImpl::ResolveInputLocation(const SymbolContext& symbol_context,
                                                              const InputLocation& input_location,
                                                              const ResolveOptions& options) const {
  // Thie skip_function_prologue option requires that symbolize be set.
  FXL_DCHECK(!options.skip_function_prologue || options.symbolize);

  switch (input_location.type) {
    case InputLocation::Type::kNone:
      return std::vector<Location>();
    case InputLocation::Type::kLine:
      return ResolveLineInputLocation(symbol_context, input_location, options);
    case InputLocation::Type::kName:
      return ResolveSymbolInputLocation(symbol_context, input_location, options);
    case InputLocation::Type::kAddress:
      return ResolveAddressInputLocation(symbol_context, input_location, options);
  }
}

LineDetails ModuleSymbolsImpl::LineDetailsForAddress(const SymbolContext& symbol_context,
                                                     uint64_t absolute_address, bool greedy) const {
  uint64_t relative_address = symbol_context.AbsoluteToRelative(absolute_address);

  llvm::DWARFCompileUnit* unit = llvm::dyn_cast_or_null<llvm::DWARFCompileUnit>(
      CompileUnitForRelativeAddress(relative_address));
  if (!unit)
    return LineDetails();
  const llvm::DWARFDebugLine::LineTable* line_table = context_->getLineTableForUnit(unit);
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
  if (found_row_index == line_table->UnknownRowIndex || rows[found_row_index].EndSequence)
    return LineDetails();

  // Adjust the beginning and end ranges to include all matching
  // entries of the same line.
  uint32_t first_row_index = found_row_index;
  while (first_row_index > 0 &&
         SameFileLine(rows[found_row_index], rows[first_row_index - 1], greedy)) {
    first_row_index--;
  }
  uint32_t last_row_index = found_row_index;
  while (last_row_index < rows.size() - 1 &&
         SameFileLine(rows[found_row_index], rows[last_row_index + 1], greedy)) {
    last_row_index++;
  }

  // Resolve the file name. Skip for "line 0" entries which are compiled-generated code not
  // associated with a line entry. Typically there will be a file if we ask, but that's leftover
  // from the previous row in the table by the state machine and is not relevant.
  std::string file_name;
  if (rows[first_row_index].Line) {
    line_table->getFileNameByIndex(rows[first_row_index].File, "",
                                   llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
                                   file_name);
  }

  LineDetails result(FileLine(file_name, unit->getCompilationDir(), rows[first_row_index].Line));

  // Add entries for each row. The last row doesn't count because it should be
  // an end_sequence marker to provide the ending size of the previous entry.
  // So never include that.
  for (uint32_t i = first_row_index; i <= last_row_index && i < rows.size() - 1; i++) {
    // With loop bounds we can always dereference @ i + 1.
    if (rows[i + 1].Address < rows[i].Address)
      break;  // Going backwards, corrupted so give up.

    LineDetails::LineEntry entry;
    entry.column = rows[i].Column;
    entry.range = AddressRange(symbol_context.RelativeToAbsolute(rows[i].Address),
                               symbol_context.RelativeToAbsolute(rows[i + 1].Address));
    result.entries().push_back(entry);
  }

  return result;
}

std::vector<std::string> ModuleSymbolsImpl::FindFileMatches(std::string_view name) const {
  return index_.FindFileMatches(name);
}

std::vector<fxl::RefPtr<Function>> ModuleSymbolsImpl::GetMainFunctions() const {
  std::vector<fxl::RefPtr<Function>> result;
  for (const auto& ref : index_.main_functions()) {
    auto symbol_ref = IndexDieRefToSymbol(ref);
    const Function* func = symbol_ref.Get()->AsFunction();
    if (func)
      result.emplace_back(RefPtrTo(func));
  }
  return result;
}

const Index& ModuleSymbolsImpl::GetIndex() const { return index_; }

LazySymbol ModuleSymbolsImpl::IndexDieRefToSymbol(const IndexNode::DieRef& die_ref) const {
  return symbol_factory_->MakeLazy(die_ref.offset());
}

bool ModuleSymbolsImpl::HasBinary() const {
  if (!binary_name_.empty())
    return true;

  if (auto debug = elflib::ElfLib::Create(name_))
    return debug->ProbeHasProgramBits();

  return false;
}

llvm::DWARFUnit* ModuleSymbolsImpl::CompileUnitForRelativeAddress(uint64_t relative_address) const {
  return compile_units_.getUnitForOffset(
      context_->getDebugAranges()->findAddress(relative_address));
}

void ModuleSymbolsImpl::AppendLocationForFunction(const SymbolContext& symbol_context,
                                                  const ResolveOptions& options,
                                                  const Function* func,
                                                  std::vector<Location>* result) const {
  if (func->code_ranges().empty())
    return;  // No code associated with this.

  // Compute the full file/line information if requested. This recomputes function DIE which is
  // unnecessary but makes the code structure simpler and ensures the results are always the same
  // with regard to how things like inlined functions are handled (if the location maps to both a
  // function and an inlined function inside of it).
  uint64_t abs_addr = symbol_context.RelativeToAbsolute(func->code_ranges()[0].begin());
  if (options.symbolize)
    result->push_back(LocationForAddress(symbol_context, abs_addr, options, func));
  else
    result->emplace_back(Location::State::kAddress, abs_addr);
}

std::vector<Location> ModuleSymbolsImpl::ResolveLineInputLocation(
    const SymbolContext& symbol_context, const InputLocation& input_location,
    const ResolveOptions& options) const {
  std::vector<Location> result;
  for (const std::string& file : FindFileMatches(input_location.line.file())) {
    ResolveLineInputLocationForFile(symbol_context, file, input_location.line.line(), options,
                                    &result);
  }
  return result;
}

std::vector<Location> ModuleSymbolsImpl::ResolveSymbolInputLocation(
    const SymbolContext& symbol_context, const InputLocation& input_location,
    const ResolveOptions& options) const {
  // Special-case for PLT functions.
  if (auto plt_name = GetPLTInputLocation(input_location))
    return ResolvePltName(symbol_context, *plt_name);

  std::vector<Location> result;

  auto symbol_to_find = input_location.name;

  // Special-case for main functions.
  if (ReferencesMainFunction(input_location)) {
    auto main_functions = GetMainFunctions();
    if (!main_functions.empty()) {
      for (const auto& func : GetMainFunctions())
        AppendLocationForFunction(symbol_context, options, func.get(), &result);
      return result;
    } else {
      // Nothing explicitly marked as the main function, fall back on anything in the toplevel
      // namespace named "main".
      symbol_to_find = Identifier(IdentifierQualification::kGlobal, IdentifierComponent("main"));

      // Fall through to symbol finding on the new name.
    }
  }

  // TODO(bug 37654) it would be nice if this could be deleted and all code go through
  // expr/find_name.h to query the index. As-is this duplicates some of FindName's logic in a less
  // flexible way.
  for (const auto& die_ref : index_.FindExact(symbol_to_find)) {
    LazySymbol lazy_symbol = IndexDieRefToSymbol(die_ref);
    const Symbol* symbol = lazy_symbol.Get();

    if (const Function* function = symbol->AsFunction()) {
      // Symbol is a function.
      AppendLocationForFunction(symbol_context, options, function, &result);
    } else if (const Variable* variable = symbol->AsVariable()) {
      // Symbol is a variable. This will be the case for global variables and file- and class-level
      // statics. This always symbolizes since we already computed the symbol.
      result.push_back(LocationForVariable(symbol_context, RefPtrTo(variable)));
    } else {
      // Unknown type of symbol.
      continue;
    }
  }

  // Fall back on ELF symbols if nothing was found. Many ELF symbols will duplicate the DWARF
  // ones so we don't want to do this if there was a DWARF match.
  if (result.empty()) {
    // Currently we only support ELF lookup by mangled name. The reason is that the unmangled
    // name for function names has a () and won't match our Identifier class. Currently ELF name
    // lookup is not really used (the DWARF symbols should have normal things people need) so this
    // is not a high priority.
    //
    // TODO(bug 41928) make Identifier support function parameters.
    return ResolveElfName(symbol_context, symbol_to_find.GetFullNameNoQual());
  }

  return result;
}

std::vector<Location> ModuleSymbolsImpl::ResolveAddressInputLocation(
    const SymbolContext& symbol_context, const InputLocation& input_location,
    const ResolveOptions& options) const {
  std::vector<Location> result;
  if (options.symbolize) {
    result.push_back(LocationForAddress(symbol_context, input_location.address, options, nullptr));
  } else {
    result.emplace_back(Location::State::kAddress, input_location.address);
  }
  return result;
}

Location ModuleSymbolsImpl::LocationForAddress(const SymbolContext& symbol_context,
                                               uint64_t absolute_address,
                                               const ResolveOptions& options,
                                               const Function* optional_func) const {
  if (auto dwarf_loc =
          DwarfLocationForAddress(symbol_context, absolute_address, options, optional_func))
    return std::move(*dwarf_loc);
  if (auto elf_locs = ElfLocationForAddress(symbol_context, absolute_address, options))
    return std::move(*elf_locs);

  // Not symbolizable, return an "address" with no symbol information. Mark it symbolized to record
  // that we tried and failed.
  return Location(Location::State::kSymbolized, absolute_address);
}

// This function is similar to llvm::DWARFContext::getLineInfoForAddress.
std::optional<Location> ModuleSymbolsImpl::DwarfLocationForAddress(
    const SymbolContext& symbol_context, uint64_t absolute_address, const ResolveOptions& options,
    const Function* optional_func) const {
  // TODO(DX-695) handle addresses that aren't code like global variables.
  uint64_t relative_address = symbol_context.AbsoluteToRelative(absolute_address);
  llvm::DWARFUnit* unit = CompileUnitForRelativeAddress(relative_address);
  if (!unit)  // No DWARF symbol.
    return std::nullopt;

  // Get the innermost subroutine or inlined function for the address. This may be empty, but still
  // lookup the line info below in case its present. This computes both a LazySymbol which we
  // pass to the result, and a possibly-null containing Function* (not an inlined subroutine) to do
  // later computations on.
  fxl::RefPtr<Function> containing_function;  // Keep in scope from GetContainingFunction().
  LazySymbol lazy_function;
  if (optional_func) {
    containing_function = RefPtrTo(optional_func);
    lazy_function = LazySymbol(optional_func);
  } else {
    llvm::DWARFDie subroutine = unit->getSubroutineForAddress(relative_address);
    if (subroutine) {
      lazy_function = symbol_factory_->MakeLazy(subroutine);
      // getSubroutineForAddress will return inline functions and we want the physical function
      // for prologue computations. Use GetContainingFunction() to get that.
      if (const CodeBlock* code_block = lazy_function.Get()->AsCodeBlock())
        containing_function = code_block->GetContainingFunction();
    }
  }

  // Get the file/line location (may fail).
  const llvm::DWARFDebugLine::LineTable* line_table = context_->getLineTableForUnit(unit);
  if (line_table) {
    if (containing_function && options.skip_function_prologue) {
      // Use the line table to move the address to after the function prologue.
      size_t prologue_size =
          GetFunctionPrologueSize(LineTableImpl(context_.get(), unit), containing_function.get());
      if (prologue_size > 0) {
        // The function has a prologue. When it does, we know it has code ranges so don't need to
        // validate it's nonempty before using.
        uint64_t function_begin = containing_function->code_ranges().front().begin();
        if (relative_address >= function_begin &&
            relative_address < function_begin + prologue_size) {
          // Adjust address to the first real instruction.
          relative_address = function_begin + prologue_size;
          absolute_address = symbol_context.RelativeToAbsolute(relative_address);
        }
      }
    }

    // Look up the line info for this address.
    //
    // This re-computes some of what GetFunctionPrologueSize() may have done above. This could be
    // enhanced in the future by having our own version of getFileLineInfoForAddress that includes
    // the prologue adjustment as part of one computation.
    llvm::DILineInfo line_info;
    if (line_table->getFileLineInfoForAddress(
            relative_address, "", llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
            line_info)) {
      // Line info present. Only set the file name if there's a nonzero line number. "Line 0"
      // entries which are compiled-generated code not associated with a line entry. Typically there
      // will be a file if we ask, but that's leftover from the previous row in the table by the
      // state machine and is not relevant.
      std::string file_name;
      if (line_info.Line)
        file_name = std::move(line_info.FileName);
      return Location(absolute_address,
                      FileLine(std::move(file_name), unit->getCompilationDir(), line_info.Line),
                      line_info.Column, symbol_context, std::move(lazy_function));
    }
  }

  // No line information.
  return Location(absolute_address, FileLine(), 0, symbol_context, std::move(lazy_function));
}

std::optional<Location> ModuleSymbolsImpl::ElfLocationForAddress(
    const SymbolContext& symbol_context, uint64_t absolute_address,
    const ResolveOptions& options) const {
  if (elf_addresses_.empty())
    return std::nullopt;

  // TODO(bug 42243) make sure the address is inside the library. Otherwise this will match
  // random addresses for the largest ELF symbol.
  uint64_t relative_addr = symbol_context.AbsoluteToRelative(absolute_address);
  auto found = LargestLessOrEqual(
      elf_addresses_.begin(), elf_addresses_.end(), relative_addr,
      [](const ElfSymbolRecord* r, uint64_t addr) { return r->relative_address < addr; },
      [](const ElfSymbolRecord* r, uint64_t addr) { return r->relative_address == addr; });
  if (found == elf_addresses_.end())
    return std::nullopt;

  // There could theoretically be multiple matches for this address, but we return only the first.
  const ElfSymbolRecord* record = *found;
  return Location(
      absolute_address, FileLine(), 0, symbol_context,
      fxl::MakeRefCounted<ElfSymbol>(const_cast<ModuleSymbolsImpl*>(this)->GetWeakPtr(), *record));
}

Location ModuleSymbolsImpl::LocationForVariable(const SymbolContext& symbol_context,
                                                fxl::RefPtr<Variable> variable) const {
  // Evaluate the DWARF expression for the variable. Global and static variables' locations aren't
  // based on CPU state. In some cases like TLS the location may require CPU state or may result in
  // a constant instead of an address. In these cases give up and return an "unlocated variable."
  // These can easily be evaluated by the expression system so we can still print their values.

  // Need one unique location.
  if (variable->location().locations().size() != 1)
    return Location(symbol_context, std::move(variable));

  auto global_data_provider = fxl::MakeRefCounted<GlobalSymbolDataProvider>();
  DwarfExprEval eval;
  eval.Eval(global_data_provider, symbol_context, variable->location().locations()[0].expression,
            [](DwarfExprEval* eval, const Err& err) {});

  // Only evaluate synchronous outputs that result in a pointer.
  if (!eval.is_complete() || !eval.is_success() ||
      eval.GetResultType() != DwarfExprEval::ResultType::kPointer)
    return Location(symbol_context, std::move(variable));

  // TODO(brettw) in all of the return cases we could in the future fill in the file/line of the
  // definition of the variable. Currently Variables don't provide that (even though it's usually in
  // the DWARF symbols).
  return Location(eval.GetResult(), FileLine(), 0, symbol_context, std::move(variable));
}

std::vector<Location> ModuleSymbolsImpl::ResolvePltName(const SymbolContext& symbol_context,
                                                        const std::string& mangled_name) const {
  // There can theoretically be multiple symbols with the given name, some might be PLT symbols,
  // some might not be. Check all name matches for a PLT one.
  auto cur = mangled_elf_symbols_.lower_bound(mangled_name);
  while (cur != mangled_elf_symbols_.end() && cur->first == mangled_name) {
    if (cur->second.type == ElfSymbolType::kPlt)
      return {MakeElfSymbolLocation(symbol_context, std::nullopt, cur->second)};
    ++cur;
  }

  // No PLT locations found for this name.
  return {};
}

std::vector<Location> ModuleSymbolsImpl::ResolveElfName(const SymbolContext& symbol_context,
                                                        const std::string& mangled_name) const {
  std::vector<Location> result;

  // There can theoretically be multiple symbols with the given name.
  auto cur = mangled_elf_symbols_.lower_bound(mangled_name);
  while (cur != mangled_elf_symbols_.end() && cur->first == mangled_name) {
    result.push_back(MakeElfSymbolLocation(symbol_context, std::nullopt, cur->second));
    ++cur;
  }

  return result;
}

// To a first approximation we just look up the line in the line table for each compilation unit
// that references the file. Complications:
//
// 1. The line might not be an exact match (the user can specify a blank line or something optimized
//    out). In this case, find the next valid line.
//
// 2. The above step can find many different locations. Maybe some code from the file in question is
//    inlined into the compilation unit, but not the function with the line in it. Or different
//    template instantiations can mean that a line of code is in some instantiations but don't apply
//    to others.
//
//    To solve this duplication problem, get the resolved line of each of the addresses found above
//    and find the best one. Keep only those locations matching the best one (there can still be
//    multiple).
//
// 3. Inlining and templates can mean there can be multiple matches of the exact same line. Only
//    keep the first match per function or inlined function to catch the case where a line is spread
//    across multiple line table entries.
void ModuleSymbolsImpl::ResolveLineInputLocationForFile(const SymbolContext& symbol_context,
                                                        const std::string& canonical_file,
                                                        int line_number,
                                                        const ResolveOptions& options,
                                                        std::vector<Location>* output) const {
  const std::vector<unsigned>* units = index_.FindFileUnitIndices(canonical_file);
  if (!units)
    return;

  std::vector<LineMatch> matches;
  for (unsigned index : *units) {
    llvm::DWARFUnit* unit = context_->getUnitAtIndex(index);
    LineTableImpl line_table(context_.get(), unit);

    // Complication 1 above: find all matches for this line in the unit.
    std::vector<LineMatch> unit_matches =
        GetAllLineTableMatchesInUnit(line_table, canonical_file, line_number);

    matches.insert(matches.end(), unit_matches.begin(), unit_matches.end());
  }

  if (matches.empty())
    return;

  // Complications 2 & 3 above: Get all instances of the best match only with a max of one per
  // function. The best match is the one with the lowest line number (found matches should all be
  // bigger than the input line, so this will be the closest).
  for (const LineMatch& match : GetBestLineMatches(matches)) {
    uint64_t abs_addr = symbol_context.RelativeToAbsolute(match.address);
    if (options.symbolize)
      output->push_back(LocationForAddress(symbol_context, abs_addr, options, nullptr));
    else
      output->push_back(Location(Location::State::kAddress, abs_addr));
  }
}

Location ModuleSymbolsImpl::MakeElfSymbolLocation(const SymbolContext& symbol_context,
                                                  std::optional<uint64_t> relative_address,
                                                  const ElfSymbolRecord& record) const {
  uint64_t absolute_address;
  if (relative_address) {
    // Caller specified a more specific address (normally inside the ELF symbol).
    absolute_address = symbol_context.RelativeToAbsolute(*relative_address);
  } else {
    // Take address from the ELF symbol.
    absolute_address = symbol_context.RelativeToAbsolute(record.relative_address);
  }

  return Location(
      absolute_address, FileLine(), 0, symbol_context,
      fxl::MakeRefCounted<ElfSymbol>(const_cast<ModuleSymbolsImpl*>(this)->GetWeakPtr(), record));
}

void ModuleSymbolsImpl::FillElfSymbols(const std::map<std::string, llvm::ELF::Elf64_Sym>& elf_syms,
                                       const std::map<std::string, uint64_t>& plt_syms) {
  FXL_DCHECK(mangled_elf_symbols_.empty());
  FXL_DCHECK(elf_addresses_.empty());

  // Insert the regular symbols.
  //
  // The |st_value| is the relative virtual address we want to index. Potentially we might want to
  // save more flags and expose them in the ElfSymbol class.
  for (const auto& [name, sym] : elf_syms) {
    // The symbol type is the low 4 bits. The higher bits encode the visibility which we don't
    // care about. We only need to index objects and code.
    int symbol_type = sym.st_info & 0xf;
    if (symbol_type != elflib::STT_OBJECT && symbol_type != elflib::STT_FUNC &&
        symbol_type != elflib::STT_TLS)
      continue;

    if (sym.st_value == 0)
      continue;  // No address for this symbol. Probably imported.

    auto inserted = mangled_elf_symbols_.emplace(
        std::piecewise_construct, std::forward_as_tuple(name),
        std::forward_as_tuple(ElfSymbolType::kNormal, sym.st_value, name));

    // Append all addresses for now, this will be sorted at the bottom.
    elf_addresses_.push_back(&inserted->second);
  }

  // Insert PLT symbols.
  for (const auto& [name, addr] : plt_syms) {
    auto inserted =
        mangled_elf_symbols_.emplace(std::piecewise_construct, std::forward_as_tuple(name),
                                     std::forward_as_tuple(ElfSymbolType::kPlt, addr, name));

    // Append all addresses for now, this will be sorted at the bottom.
    elf_addresses_.push_back(&inserted->second);
  }

  std::sort(elf_addresses_.begin(), elf_addresses_.end(),
            [](const ElfSymbolRecord* left, const ElfSymbolRecord* right) {
              return left->relative_address < right->relative_address;
            });
}

}  // namespace zxdb
