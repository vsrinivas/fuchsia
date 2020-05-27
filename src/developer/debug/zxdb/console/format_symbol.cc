// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_symbol.h"

#include <algorithm>
#include <optional>

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/resolve_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

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
    const InheritedFrom* from = lazy_from.Get()->AsInheritedFrom();
    if (!from)
      continue;

    auto from_type = GetConcreteType(find_name_context, from->from().Get()->AsType());
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
    const DataMember* member = lazy_member.Get()->AsDataMember();
    if (!member)
      continue;

    const Type* member_type = member->type().Get()->AsType();
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

  if (records.empty()) {
    out.Append("  Members: <none>\n");
    return out;
  }
  out.Append("  Members:\n");

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
  out.Append(fxl::StringPrintf("  %s: ", heading));
  out.Append(GetFormattedName(lazy_type.Get()->AsType()));
  out.Append("\n");
  return out;
}

// Creates a compilation unit line for the given symbol. If there is none (normally during testing),
// returns an empty buffer.
OutputBuffer FormatCompilationUnit(const Symbol* symbol) {
  OutputBuffer out;

  auto compile_unit = symbol->GetCompileUnit();
  if (!compile_unit)
    return out;

  out.Append("  Compilation unit: ");
  out.Append(compile_unit->name());
  out.Append("\n");

  return out;
}

OutputBuffer FormatVariableLocation(const SymbolContext& symbol_context,
                                    const VariableLocation& loc) {
  OutputBuffer out;
  if (loc.is_null()) {
    out.Append("  DWARF location: <no location info>\n");
    return out;
  }

  out.Append("  DWARF location (address range + DWARF expression bytes):\n");
  for (const auto& entry : loc.locations()) {
    // Address range.
    if (entry.begin == 0 && entry.end == 0) {
      out.Append("    <always valid>:");
    } else {
      out.Append(fxl::StringPrintf(
          "    [0x%" PRIx64 ", 0x%" PRIx64 "):", symbol_context.RelativeToAbsolute(entry.begin),
          symbol_context.RelativeToAbsolute(entry.end)));
    }

    // Dump the raw DWARF expression bytes. In the future we can decode if necessary (check LLVM's
    // "dwarfdump" utility which can do this).
    for (uint8_t byte : entry.expression)
      out.Append(fxl::StringPrintf(" 0x%02x", byte));
    out.Append("\n");
  }

  return out;
}

OutputBuffer FormatType(const ProcessSymbols* process_symbols, const Type* type) {
  OutputBuffer out;
  out.Append(Syntax::kHeading, "Type: ");
  out.Append(GetFormattedName(type));

  out.Append("\n  DWARF tag: " + DwarfTagToString(type->tag(), true) + "\n");
  // Write the compilation unit for types. Some types can list many matches from multiple units,
  // so this makes it more clear what's happening.
  out.Append(FormatCompilationUnit(type));
  out.Append("  Byte size: " + std::to_string(type->byte_size()) + "\n");

  // Subtype-specific handling.
  if (const BaseType* base_type = type->AsBaseType()) {
    out.Append("  DWARF base type: " + BaseType::BaseTypeToString(base_type->base_type(), true) +
               "\n");
  } else if (const Collection* collection = type->AsCollection()) {
    out.Append(FormatCollectionMembers(process_symbols, collection));
  } else if (const ModifiedType* modified = type->AsModifiedType()) {
    if (modified->tag() == DwarfTag::kTypedef) {
      out.Append(FormatTypeDescription("Underlying type", modified->modified()));

      // For typedefs of collections, show the collection members. Often people won't know such
      // a thing is a typedef and doing this can save a step. Additionally, in C it's common to do
      // "typedef struct { ... } Name;" which creates a typedef of an anonymous struct. There's no
      // way to refer to the underlying struct so putting them here is the only way to see them.
      if (const Collection* modified_collection = modified->modified().Get()->AsCollection())
        out.Append(FormatCollectionMembers(process_symbols, modified_collection));
    } else {
      out.Append(FormatTypeDescription("Modified type", modified->modified()));
    }
  }

  return out;
}

OutputBuffer FormatFunction(const SymbolContext& symbol_context, const Function* function) {
  OutputBuffer out;

  if (function->is_inline())
    out.Append(Syntax::kHeading, "Inline function: ");
  else
    out.Append(Syntax::kHeading, "Function: ");

  FormatFunctionNameOptions opts;
  opts.name.bold_last = true;
  opts.params = FormatFunctionNameOptions::kParamTypes;

  out.Append(FormatFunctionName(function, opts));
  out.Append("\n");

  // Code ranges.
  AddressRanges ranges = function->GetAbsoluteCodeRanges(symbol_context);
  if (ranges.empty()) {
    out.Append("  No code ranges.\n");
  } else {
    out.Append("  Code ranges [begin, end-non-inclusive):\n");
    for (const auto& range : ranges)
      out.Append("    " + range.ToString() + "\n");
  }
  return out;
}

OutputBuffer FormatVariable(const SymbolContext& symbol_context, const Variable* variable) {
  OutputBuffer out;
  out.Append(Syntax::kHeading, "Variable: ");
  out.Append(Syntax::kVariable, variable->GetAssignedName());
  out.Append("\n");
  out.Append(FormatTypeDescription("Type", variable->type()));
  out.Append("  DWARF tag: " + DwarfTagToString(variable->tag(), true) + "\n");

  out.Append(FormatVariableLocation(symbol_context, variable->location()));

  return out;
}

OutputBuffer FormatDataMember(const DataMember* data_member) {
  OutputBuffer out;
  out.Append(Syntax::kHeading, "Data member: ");
  out.Append(Syntax::kVariable, data_member->GetFullName() + "\n");

  auto parent = data_member->parent().Get();
  out.Append("  Contained in: ");
  out.Append(FormatIdentifier(parent->GetIdentifier(), FormatIdentifierOptions()));
  out.Append("\n");

  out.Append(FormatTypeDescription("Type", data_member->type()));
  out.Append(fxl::StringPrintf("  Offset within container: %" PRIu32 "\n",
                               data_member->member_location()));
  out.Append("  DWARF tag: " + DwarfTagToString(data_member->tag(), true) + "\n");

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

  out.Append("  Address: " +
             to_hex_string(symbol_context.RelativeToAbsolute(elf_symbol->relative_address())) +
             "\n");
  out.Append("  Size: " + to_hex_string(elf_symbol->size()) + "\n");
  return out;
}

OutputBuffer FormatOtherSymbol(const Symbol* symbol) {
  OutputBuffer out;
  out.Append(Syntax::kHeading, "Other symbol: ");
  out.Append(symbol->GetFullName() + "\n");
  return out;
}

}  // namespace

OutputBuffer FormatSymbol(const ProcessSymbols* process_symbols, const Symbol* symbol) {
  SymbolContext symbol_context = symbol->GetSymbolContext(process_symbols);

  if (const Type* type = symbol->AsType())
    return FormatType(process_symbols, type);
  if (const Function* function = symbol->AsFunction())
    return FormatFunction(symbol_context, function);
  if (const Variable* variable = symbol->AsVariable())
    return FormatVariable(symbol_context, variable);
  if (const DataMember* data_member = symbol->AsDataMember())
    return FormatDataMember(data_member);
  if (const ElfSymbol* elf_symbol = symbol->AsElfSymbol())
    return FormatElfSymbol(symbol_context, elf_symbol);

  return FormatOtherSymbol(symbol);
}

}  // namespace zxdb
