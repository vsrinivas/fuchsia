// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_symbol.h"

#include <algorithm>
#include <optional>

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/resolve_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/call_site.h"
#include "src/developer/debug/zxdb/symbols/call_site_parameter.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_status.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

std::string GetIndentStr(int indent_level) { return std::string(indent_level * 2, ' '); }

// Handles formatting with pretty identifier formatting if possible, and falls back to raw strings
// for cases where the name isn't an identifier (e.g. modified types like "const int*").
//
// The input can be null which will print out an error.
OutputBuffer GetFormattedName(const Symbol* symbol) {
  OutputBuffer out;
  if (!symbol) {
    out.Append(Syntax::kComment, "<bad symbol>");
    return out;
  }

  const Identifier& ident = symbol->GetIdentifier();
  if (ident.empty()) {
    out.Append(symbol->GetFullName());
  } else {
    FormatIdentifierOptions options;
    options.bold_last = true;
    out.Append(FormatIdentifier(ident, options));
  }

  return out;
}

// ProcessSymbols can be null.
OutputBuffer FormatCollectionMembers(const ProcessSymbols* process_symbols,
                                     const Collection* coll) {
  OutputBuffer out;

  struct Record {
    Record(std::optional<size_t> o, size_t s, OutputBuffer n, OutputBuffer t)
        : offset(o), size(s), name(std::move(n)), type(std::move(t)) {}

    std::optional<size_t> offset;  // nullopt for virtual inheritance where the offset is not known.
    size_t size;
    OutputBuffer name;
    OutputBuffer type;
  };
  std::vector<Record> records;

  FindNameContext find_name_context(process_symbols);  // FindNameContext handles null pointers.

  // Inherited base classes.
  for (const auto& lazy_from : coll->inherited_from()) {
    const InheritedFrom* from = lazy_from.Get()->As<InheritedFrom>();
    if (!from)
      continue;

    auto from_type = GetConcreteType(find_name_context, from->from());
    if (!from_type)
      continue;

    std::optional<size_t> offset;
    if (from->kind() == InheritedFrom::kConstant)
      offset = from->offset();

    records.emplace_back(offset, from_type->byte_size(),
                         OutputBuffer(Syntax::kComment, "<base class>"),
                         GetFormattedName(from_type.get()));
  }

  // Data members.
  for (const auto& lazy_member : coll->data_members()) {
    const DataMember* member = lazy_member.Get()->As<DataMember>();
    if (!member)
      continue;

    const Type* member_type = member->type().Get()->As<Type>();
    if (!member_type)
      continue;

    // TODO(brettw) We should probably show bitfields here.
    records.emplace_back(member->member_location(), member_type->byte_size(),
                         OutputBuffer(Syntax::kVariable, member->GetAssignedName()),
                         GetFormattedName(member_type));
  }

  // Sort by data offset. Use the stable sort to keep inherited base classes first even if they
  // start at the same offset as a data member (they can be 0 size).
  std::stable_sort(records.begin(), records.end(),
                   [](const Record& a, const Record& b) { return a.offset < b.offset; });

  out.Append(Syntax::kHeading, "  Members:");
  if (records.empty()) {
    out.Append(" <empty>\n");
    return out;
  }
  out.Append("\n");

  // Construct into table rows.
  std::vector<std::vector<OutputBuffer>> rows;
  size_t prev_end = 0;  // Next byte after the last one we've processed.
  for (Record& record : records) {
    OutputBuffer offset_desc;

    if (record.offset) {
      if (record.offset > prev_end) {
        // Found empty space. Indicate this.
        auto& row = rows.emplace_back();
        row.emplace_back(Syntax::kComment, std::to_string(prev_end));
        row.emplace_back(Syntax::kComment, std::to_string(*record.offset - prev_end));
        row.emplace_back();
        row.emplace_back(Syntax::kComment, "<padding>");
      }
      offset_desc = OutputBuffer(std::to_string(*record.offset));
    } else {
      // Virtual inheritance.
      offset_desc = OutputBuffer(Syntax::kComment, "<virtual>");
    }

    auto& row = rows.emplace_back();
    row.push_back(std::move(offset_desc));
    row.emplace_back(std::to_string(record.size));
    row.push_back(std::move(record.name));
    row.push_back(std::move(record.type));

    if (record.offset) {
      // The std::max() call is necessary so we always go forward. Sometimes inheritance information
      // for zero-sized base classes can overlap.
      prev_end = std::max(prev_end, *record.offset + record.size);
    }
  }

  FormatTable({ColSpec(Align::kRight, 0, "Offset", 4), ColSpec(Align::kRight, 0, "Size"),
               ColSpec(Align::kLeft, 0, "Name"), ColSpec(Align::kLeft, 0, "Type")},
              rows, &out);
  return out;
}

// Formats a type as a one-line member for use while dumping a symbol that has a type. The heading
// will be followed with a colon to provide a label.
OutputBuffer FormatTypeDescription(const char* heading, const LazySymbol& lazy_type) {
  OutputBuffer out;
  out.Append(Syntax::kHeading, fxl::StringPrintf("  %s: ", heading));
  // DWARF uses empty types for "void".
  if (lazy_type) {
    out.Append(GetFormattedName(lazy_type.Get()->As<Type>()));
  } else {
    out.Append("void");
  }
  out.Append("\n");
  return out;
}

// Creates a compilation unit and module line for the given symbol. If there is none (normally
// during testing), returns an empty buffer.
OutputBuffer FormatCompilationUnitAndModule(int indent, const Symbol* symbol) {
  OutputBuffer out;

  auto compile_unit = symbol->GetCompileUnit();
  if (!compile_unit)
    return out;

  std::string indent_str = GetIndentStr(indent);

  if (fxl::WeakPtr<ModuleSymbols> module = symbol->GetModuleSymbols()) {
    ModuleSymbolStatus status = module->GetStatus();
    if (!status.name.empty()) {
      out.Append(Syntax::kHeading, indent_str + "  Module: ");
      out.Append(Syntax::kFileName, status.name);
      out.Append("\n");
    }
  }

  out.Append(Syntax::kHeading, indent_str + "  Compilation unit: ");
  out.Append(Syntax::kFileName, compile_unit->name());
  out.Append("\n");

  return out;
}

// Implements SymbolDataProvider just enough for the DwarfExprEval to pretty-print register names.
class ArchDataProvider : public SymbolDataProvider {
 public:
  ArchDataProvider(debug::Arch a) : arch_(a) {}

  debug::Arch GetArch() override { return arch_; }

 private:
  debug::Arch arch_;
};

// Format the given DwarfExpr, does not include a newline at the end.
OutputBuffer FormatDwarfExpr(debug::Arch arch, FormatSymbolOptions::DwarfExpr what,
                             const SymbolContext& symbol_context, const DwarfExpr& expr) {
  if (what == FormatSymbolOptions::DwarfExpr::kBytes) {
    // Dump the raw DWARF expression bytes.
    std::string result;
    bool first = true;
    for (uint8_t byte : expr.data()) {
      if (first) {
        first = false;
      } else {
        result.push_back(' ');  // Separator between bytes.
      }
      result += to_hex_string(byte, 2);
    }
    return result;
  }

  // Stringifying does not require DIE lookups so we can pass an empty UnitSymbolFactory.
  DwarfExprEval eval(UnitSymbolFactory(), fxl::MakeRefCounted<ArchDataProvider>(arch),
                     symbol_context);
  return eval.ToString(expr, what == FormatSymbolOptions::DwarfExpr::kPretty);
}

OutputBuffer FormatVariableLocation(int indent, const std::string& title,
                                    const SymbolContext& symbol_context,
                                    const VariableLocation& loc, const FormatSymbolOptions& opts) {
  std::string indent_str = GetIndentStr(indent);

  OutputBuffer out;
  if (loc.is_null()) {
    out.Append(Syntax::kHeading, indent_str + title + ":");
    out.Append(Syntax::kComment, " <no location info>\n");
    return out;
  }

  out.Append(Syntax::kHeading, indent_str + title);
  out.Append(Syntax::kComment, " (address range + DWARF expression):\n");
  for (const auto& entry : loc.locations()) {
    out.Append(indent_str +
               fxl::StringPrintf("  [0x%" PRIx64 ", 0x%" PRIx64 "): ",
                                 symbol_context.RelativeToAbsolute(entry.range.begin()),
                                 symbol_context.RelativeToAbsolute(entry.range.end())));
    out.Append(FormatDwarfExpr(opts.arch, opts.dwarf_expr, symbol_context, entry.expression));
    out.Append("\n");
  }

  if (const DwarfExpr* default_expr = loc.default_expr()) {
    out.Append(Syntax::kComment, indent_str + "  <default>: ");
    out.Append(FormatDwarfExpr(opts.arch, opts.dwarf_expr, symbol_context, *default_expr));
    out.Append("\n");
  }

  return out;
}

std::string FormatDieTagAndOffset(const Symbol* symbol) {
  std::string out = DwarfTagToString(symbol->tag(), true);
  if (uint64_t die_offset = symbol->GetDieOffset()) {
    out.append(" @ offset ");
    out.append(to_hex_string(die_offset));
  } else {
    out.append(" (synthetic symbol)");
  }
  return out;
}

OutputBuffer FormatType(const ProcessSymbols* process_symbols, const Type* type) {
  OutputBuffer out;
  out.Append(Syntax::kHeading, "Type: ");
  out.Append(GetFormattedName(type));

  out.Append(Syntax::kHeading, "\n  DWARF tag: ");
  out.Append(FormatDieTagAndOffset(type) + "\n");
  out.Append(FormatCompilationUnitAndModule(0, type));
  out.Append(Syntax::kHeading, "  Byte size: ");
  out.Append(std::to_string(type->byte_size()) + "\n");

  // Subtype-specific handling.
  if (const BaseType* base_type = type->As<BaseType>()) {
    out.Append(Syntax::kHeading, "  DWARF base type: ");
    out.Append(BaseType::BaseTypeToString(base_type->base_type(), true) + "\n");
  } else if (const Collection* collection = type->As<Collection>()) {
    out.Append(Syntax::kHeading, "  Calling convention: ");
    out.Append(Collection::CallingConventionToString(collection->calling_convention()));
    out.Append("\n");
    out.Append(FormatCollectionMembers(process_symbols, collection));
  } else if (const ModifiedType* modified = type->As<ModifiedType>()) {
    if (modified->tag() == DwarfTag::kTypedef) {
      out.Append(FormatTypeDescription("Underlying type", modified->modified()));

      // For typedefs of collections, show the collection members. Often people won't know such
      // a thing is a typedef and doing this can save a step. Additionally, in C it's common to do
      // "typedef struct { ... } Name;" which creates a typedef of an anonymous struct. There's no
      // way to refer to the underlying struct so putting them here is the only way to see them.
      if (const Collection* modified_collection = modified->modified().Get()->As<Collection>())
        out.Append(FormatCollectionMembers(process_symbols, modified_collection));
    } else {
      out.Append(FormatTypeDescription("Modified type", modified->modified()));
    }
  }

  return out;
}

OutputBuffer FormatVariable(const std::string& heading, int indent,
                            const SymbolContext& symbol_context, const Variable* variable,
                            const FormatSymbolOptions& opts) {
  std::string indent_str = GetIndentStr(indent);

  OutputBuffer out;
  out.Append(Syntax::kHeading, indent_str + heading + ": ");
  out.Append(Syntax::kVariable, variable->GetAssignedName());
  out.Append("\n" + indent_str);
  out.Append(FormatTypeDescription("Type", variable->type()));
  out.Append(FormatCompilationUnitAndModule(indent, variable));
  out.Append(Syntax::kHeading, indent_str + "  DWARF tag: ");
  out.Append(FormatDieTagAndOffset(variable) + "\n");

  out.Append(FormatVariableLocation(indent + 1, "DWARF location", symbol_context,
                                    variable->location(), opts));

  return out;
}

OutputBuffer FormatFunction(const SymbolContext& symbol_context, const Function* function,
                            const FormatSymbolOptions& opts) {
  OutputBuffer out;

  // Type and name.
  if (function->is_inline()) {
    out.Append(Syntax::kHeading, "Inline function: ");
  } else {
    out.Append(Syntax::kHeading, "Function: ");
  }

  FormatFunctionNameOptions name_opts;
  name_opts.name.bold_last = true;
  name_opts.params = FormatFunctionNameOptions::kParamTypes;

  out.Append(FormatFunctionName(function, name_opts));
  out.Append("\n");

  out.Append(Syntax::kHeading, "  DWARF tag: ");
  out.Append(FormatDieTagAndOffset(function) + "\n");

  // Linkage name.
  if (!function->linkage_name().empty()) {
    out.Append(Syntax::kHeading, "  Linkage name: ");
    out.Append(function->linkage_name());
    out.Append("\n");
  }

  // Declaration.
  if (function->decl_line().is_valid()) {
    out.Append(Syntax::kHeading, "  Declaration: ");
    out.Append(FormatFileLine(function->decl_line()));
    out.Append("\n");
  }

  // Call location.
  if (function->call_line().is_valid()) {
    out.Append(Syntax::kHeading, "  Inline call location: ");
    out.Append(FormatFileLine(function->call_line()));
    out.Append("\n");
  }

  // Code ranges.
  AddressRanges ranges = function->GetAbsoluteCodeRanges(symbol_context);
  if (ranges.empty()) {
    out.Append("  No code ranges.\n");
  } else {
    out.Append(Syntax::kHeading, "  Code ranges");
    out.Append(Syntax::kComment, " [begin, end-non-inclusive):\n");
    for (const auto& range : ranges)
      out.Append("    " + range.ToString() + "\n");
  }

  out.Append(FormatVariableLocation(1, "Frame base", symbol_context, function->frame_base(), opts));
  out.Append(FormatTypeDescription("Return type", function->return_type()));

  // Object pointer.
  if (const Variable* object = function->GetObjectPointerVariable())
    out.Append(FormatVariable("Object", 1, symbol_context, object, opts));

  return out;
}

OutputBuffer FormatDataMember(const DataMember* data_member) {
  OutputBuffer out;
  out.Append(Syntax::kHeading, "Data member: ");
  out.Append(Syntax::kVariable, data_member->GetFullName() + "\n");

  auto parent = data_member->parent().Get();
  out.Append(Syntax::kHeading, "  Contained in: ");
  out.Append(FormatIdentifier(parent->GetIdentifier(), FormatIdentifierOptions()));
  out.Append("\n");

  out.Append(FormatTypeDescription("Type", data_member->type()));
  out.Append(Syntax::kHeading, "  Offset within container: ");
  out.Append(fxl::StringPrintf("%" PRIu32 "\n", data_member->member_location()));
  out.Append(Syntax::kHeading, "  DWARF tag: ");
  out.Append(FormatDieTagAndOffset(data_member) + "\n");

  return out;
}

OutputBuffer FormatElfSymbol(const SymbolContext& symbol_context, const ElfSymbol* elf_symbol) {
  OutputBuffer out;
  switch (elf_symbol->elf_type()) {
    case ElfSymbolType::kNormal:
      out.Append(Syntax::kHeading, "ELF symbol: ");
      break;
    case ElfSymbolType::kPlt:
      out.Append(Syntax::kHeading, "ELF PLT symbol: ");
      break;
  }
  out.Append(elf_symbol->linkage_name() + "\n");

  out.Append(Syntax::kHeading, "  Address: ");
  out.Append(to_hex_string(symbol_context.RelativeToAbsolute(elf_symbol->relative_address())) +
             "\n");
  out.Append(Syntax::kHeading, "  Size: ");
  out.Append(to_hex_string(elf_symbol->size()) + "\n");
  return out;
}

OutputBuffer FormatOtherSymbol(const Symbol* symbol) {
  OutputBuffer out;
  out.Append(Syntax::kHeading, "Other symbol: ");
  out.Append(symbol->GetFullName() + "\n");
  out.Append(Syntax::kHeading, "  DWARF tag: ");
  out.Append(FormatDieTagAndOffset(symbol) + "\n");
  return out;
}

OutputBuffer FormatCallSiteParameter(const SymbolContext& symbol_context,
                                     const CallSiteParameter* param,
                                     const FormatSymbolOptions& opts, int indent) {
  OutputBuffer out;
  std::string indent_str = GetIndentStr(indent);

  out.Append(Syntax::kHeading,
             indent_str + "Call site parameter:\n  " + indent_str + "DWARF register #: ");
  if (param->location_register_num()) {
    out.Append(std::to_string(*param->location_register_num()));
  } else {
    out.Append(Syntax::kComment, "<unspecified>");
  }

  out.Append(Syntax::kHeading, "\n" + indent_str + "  Value expression: ");
  out.Append(FormatDwarfExpr(opts.arch, opts.dwarf_expr, symbol_context, param->value_expr()));
  out.Append("\n");

  return out;
}

OutputBuffer FormatCallSite(const SymbolContext& symbol_context, const CallSite* call_site,
                            const FormatSymbolOptions& opts) {
  OutputBuffer out;
  out.Append(Syntax::kHeading, "Call Site\n  DWARF tag: ");
  out.Append(FormatDieTagAndOffset(call_site) + "\n");

  out.Append(Syntax::kHeading, "  Return to: ");
  if (call_site->return_pc()) {
    out.Append(to_hex_string(symbol_context.RelativeToAbsolute(*call_site->return_pc())));
  } else {
    out.Append(Syntax::kComment, "<not specified>");
  }

  out.Append(Syntax::kHeading, "\n  Parameters:\n");
  for (const auto& lazy : call_site->parameters()) {
    if (const CallSiteParameter* param = lazy.Get()->As<CallSiteParameter>())
      out.Append(FormatCallSiteParameter(symbol_context, param, opts, 2));
  }
  if (call_site->parameters().empty())
    out.Append(Syntax::kComment, "    <no parameters>");

  return out;
}

}  // namespace

OutputBuffer FormatSymbol(const ProcessSymbols* process_symbols, const Symbol* symbol,
                          const FormatSymbolOptions& opts) {
  SymbolContext symbol_context = symbol->GetSymbolContext(process_symbols);

  if (const Type* type = symbol->As<Type>())
    return FormatType(process_symbols, type);
  if (const CallSite* call_site = symbol->As<CallSite>())
    return FormatCallSite(symbol_context, call_site, opts);
  if (const CallSiteParameter* call_site_param = symbol->As<CallSiteParameter>())
    return FormatCallSiteParameter(symbol_context, call_site_param, opts, 0);
  if (const Function* function = symbol->As<Function>())
    return FormatFunction(symbol_context, function, opts);
  if (const Variable* variable = symbol->As<Variable>())
    return FormatVariable("Variable", 0, symbol_context, variable, opts);
  if (const DataMember* data_member = symbol->As<DataMember>())
    return FormatDataMember(data_member);
  if (const ElfSymbol* elf_symbol = symbol->As<ElfSymbol>())
    return FormatElfSymbol(symbol_context, elf_symbol);

  return FormatOtherSymbol(symbol);
}

ErrOr<FormatSymbolOptions> GetFormatSymbolOptionsFromCommand(const Command& cmd, int expr_switch) {
  FormatSymbolOptions opts;
  opts.arch = cmd.target()->session()->arch();

  if (cmd.HasSwitch(expr_switch)) {
    std::string expr_value = cmd.GetSwitchValue(expr_switch);
    if (expr_value == "bytes") {
      opts.dwarf_expr = FormatSymbolOptions::DwarfExpr::kBytes;
    } else if (expr_value == "ops") {
      opts.dwarf_expr = FormatSymbolOptions::DwarfExpr::kOps;
    } else if (expr_value == "pretty") {
      opts.dwarf_expr = FormatSymbolOptions::DwarfExpr::kPretty;
    } else {
      return Err("Expected 'bytes', 'ops', or 'pretty' for DWARF expression format.");
    }
  }

  return opts;
}

}  // namespace zxdb
