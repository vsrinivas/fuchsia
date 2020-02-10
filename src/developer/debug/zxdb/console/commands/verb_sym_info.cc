// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_sym_info.h"

#include "llvm/Demangle/Demangle.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kSymInfoShortHelp[] = "sym-info: Print information about a symbol.";
const char kSymInfoHelp[] =
    R"(sym-info <name>

  Displays information about a given named symbol.

  It will also show the demangled name if the input is a mangled symbol.

Example

  sym-info i
  thread 1 frame 4 sym-info i
)";

void DumpVariableLocation(const SymbolContext& symbol_context, const VariableLocation& loc,
                          OutputBuffer* out) {
  if (loc.is_null()) {
    out->Append("  DWARF location: <no location info>\n");
    return;
  }

  out->Append("  DWARF location (address range + DWARF expression bytes):\n");
  for (const auto& entry : loc.locations()) {
    // Address range.
    if (entry.begin == 0 && entry.end == 0) {
      out->Append("    <always valid>:");
    } else {
      out->Append(fxl::StringPrintf(
          "    [0x%" PRIx64 ", 0x%" PRIx64 "):", symbol_context.RelativeToAbsolute(entry.begin),
          symbol_context.RelativeToAbsolute(entry.end)));
    }

    // Dump the raw DWARF expression bytes. In the future we can decode if necessary (check LLVM's
    // "dwarfdump" utility which can do this).
    for (uint8_t byte : entry.expression)
      out->Append(fxl::StringPrintf(" 0x%02x", byte));
    out->Append("\n");
  }
}

// Appends a type description for another synbol dump section.
void DumpTypeDescription(const LazySymbol& lazy_type, OutputBuffer* out) {
  out->Append("  Type: ");
  if (const Type* type = lazy_type.Get()->AsType()) {
    // Use GetFullName() instead of GetIdentifier() because modified types like pointers don't
    // map onto identifiers.
    out->Append(type->GetFullName());
  } else {
    out->Append(Syntax::kError, "[Bad type]");
  }
  out->Append("\n");
}

// ProcessSymbols can be null which will produce relative addresses.
void DumpVariableInfo(const ProcessSymbols* process_symbols, const Variable* variable,
                      OutputBuffer* out) {
  out->Append(Syntax::kHeading, "Variable: ");
  out->Append(Syntax::kVariable, variable->GetAssignedName());
  out->Append("\n");
  DumpTypeDescription(variable->type(), out);
  out->Append(fxl::StringPrintf("  DWARF tag: 0x%02x\n", static_cast<unsigned>(variable->tag())));

  DumpVariableLocation(variable->GetSymbolContext(process_symbols), variable->location(), out);
}

void DumpDataMemberInfo(const DataMember* data_member, OutputBuffer* out) {
  out->Append(Syntax::kHeading, "Data member: ");
  out->Append(Syntax::kVariable, data_member->GetFullName() + "\n");

  auto parent = data_member->parent().Get();
  out->Append("  Contained in: ");
  out->Append(FormatIdentifier(parent->GetIdentifier(), FormatIdentifierOptions()));
  out->Append("\n");

  DumpTypeDescription(data_member->type(), out);
  out->Append(fxl::StringPrintf("  Offset within container: %" PRIu32 "\n",
                                data_member->member_location()));
  out->Append(
      fxl::StringPrintf("  DWARF tag: 0x%02x\n", static_cast<unsigned>(data_member->tag())));
}

void DumpTypeInfo(const Type* type, OutputBuffer* out) {
  out->Append(Syntax::kHeading, "Type: ");
  out->Append(FormatIdentifier(type->GetIdentifier(), FormatIdentifierOptions()));
  out->Append("\n");

  out->Append(fxl::StringPrintf("  DWARF tag: 0x%02x\n", static_cast<unsigned>(type->tag())));
}

void DumpFunctionInfo(const ProcessSymbols* process_symbols, const Function* function,
                      OutputBuffer* out) {
  if (function->is_inline())
    out->Append(Syntax::kHeading, "Inline function: ");
  else
    out->Append(Syntax::kHeading, "Function: ");

  FormatFunctionNameOptions opts;
  opts.name.bold_last = true;
  opts.params = FormatFunctionNameOptions::kParamTypes;

  out->Append(FormatFunctionName(function, opts));
  out->Append("\n");

  // Code ranges.
  AddressRanges ranges =
      function->GetAbsoluteCodeRanges(function->GetSymbolContext(process_symbols));
  if (ranges.empty()) {
    out->Append("  No code ranges.\n");
  } else {
    out->Append("  Code ranges [begin, end-non-inclusive):\n");
    for (const auto& range : ranges)
      out->Append("    " + range.ToString() + "\n");
  }
}

// Demangles specifically for sym-info (this attempts to filter out simple type remapping which
// would normally be desirable for a generic demangler). Returns a nullopt on failure.
std::optional<std::string> DemangleForSymInfo(const ParsedIdentifier& identifier) {
  std::string full_input = identifier.GetFullNameNoQual();
  if (full_input.empty() || full_input[0] != '_') {
    // Filter out all names that don't start with underscores. sym-info is mostly used to look up
    // functions and variables. Functions should be demangled, but variables shouldn't. The problem
    // is that some common variables like "f" and "i" demangle to "float" and "int" respectively
    // which is not what the user wants. By only unmangling when things start with an underscore,
    // we mostly restrict to mangled function names.
    return std::nullopt;
  }

  std::optional<std::string> result;

  // TODO(brettw) use "demangled = llvm::demangle() when we roll LLVM. It avoids the buffer
  // allocation problem.
  int demangle_status = llvm::demangle_unknown_error;
  char* demangled_buf =
      llvm::itaniumDemangle(full_input.c_str(), nullptr, nullptr, &demangle_status);
  if (demangle_status == llvm::demangle_success && full_input != demangled_buf)
    result.emplace(demangled_buf);
  if (demangled_buf)
    free(demangled_buf);

  return result;
}

Err RunVerbSymInfo(ConsoleContext* context, const Command& cmd) {
  if (cmd.args().empty())
    return Err("sym-info expects the name of the symbol to look up.");

  // Type names can have spaces in them, so concatenate all args.
  std::string ident_string = cmd.args()[0];
  for (size_t i = 1; i < cmd.args().size(); i++) {
    ident_string += " ";
    ident_string += cmd.args()[i];
  }

  ParsedIdentifier identifier;
  Err err = ExprParser::ParseIdentifier(ident_string, &identifier);
  if (err.has_error())
    return err;

  // See if it looks mangled.
  OutputBuffer out;
  if (std::optional<std::string> demangled = DemangleForSymInfo(identifier)) {
    out.Append(Syntax::kHeading, "Demangled name: ");

    // Output the demangled name as a colored identifier if possible.
    ParsedIdentifier demangled_identifier;
    if (ExprParser::ParseIdentifier(*demangled, &demangled_identifier).has_error()) {
      // Not parseable as an identifier, just use the raw string.
      out.Append(*demangled);
    } else {
      out.Append(FormatIdentifier(demangled_identifier, FormatIdentifierOptions()));

      // Use the demangled name to do the lookup.
      //
      // TODO(brettw) this might need to be revisited if the index supports lookup by mangled name.
      // It would probably be best to look up both variants and compute the union.
      //
      // TODO(brettw) generally function lookup from this point will fail because our looker-upper
      // doesn't support function parameters, but the denamgled output will include the parameter
      // types or at least "()".
      identifier = std::move(demangled_identifier);
    }
    out.Append("\n\n");
  }

  ProcessSymbols* process_symbols = nullptr;
  FindNameContext find_context;
  if (cmd.target()->GetProcess()) {
    // The symbol context parameter is used to prioritize symbols from the current module but since
    // we query everything, it doesn't matter. FindNameContext will handle a null frame pointer and
    // just skip local variables in that case.
    process_symbols = cmd.target()->GetProcess()->GetSymbols();
    const CodeBlock* code_block =
        cmd.frame() ? cmd.frame()->GetLocation().symbol().Get()->AsCodeBlock() : nullptr;
    find_context =
        FindNameContext(process_symbols, SymbolContext::ForRelativeAddresses(), code_block);
  } else {
    // Non-running process. Can do some lookup for some things.
    find_context = FindNameContext(cmd.target()->GetSymbols());
  }

  FindNameOptions find_opts(FindNameOptions::kAllKinds);
  find_opts.max_results = std::numeric_limits<size_t>::max();

  std::vector<FoundName> found_items;
  FindName(find_context, find_opts, identifier, &found_items);

  bool found_item = false;
  for (const FoundName& found : found_items) {
    switch (found.kind()) {
      case FoundName::kNone:
        break;
      case FoundName::kVariable:
        // This uses the symbol context from the current frame's location. This usually works as
        // all local variables will necessarily be from the current module. DumpVariableInfo
        // only needs the symbol context for showing valid code ranges, which globals from other
        // modules won't have.
        //
        // TODO(bug 41540) look up the proper symbol context for the variable symbol object. As
        // described above this won't change most things, but we might start needing the symbol
        // context for more stuff, and it's currently very brittle.
        DumpVariableInfo(process_symbols, found.variable(), &out);
        found_item = true;
        break;
      case FoundName::kMemberVariable:
        DumpDataMemberInfo(found.member().data_member(), &out);
        found_item = true;
        break;
      case FoundName::kNamespace:
        // Probably useless to display info on a namespace.
        break;
      case FoundName::kTemplate:
        // TODO(brettw) it would be nice to list all template specializations here.
        break;
      case FoundName::kType:
        DumpTypeInfo(found.type().get(), &out);
        found_item = true;
        break;
      case FoundName::kFunction:
        DumpFunctionInfo(process_symbols, found.function().get(), &out);
        found_item = true;
        break;
    }
  }

  if (!found_item) {
    out.Append("No symbol \"");
    out.Append(FormatIdentifier(identifier, FormatIdentifierOptions()));
    out.Append("\" found in the current context.\n");
  }
  if (!out.empty())
    Console::get()->Output(out);
  return Err();
}

}  // namespace

VerbRecord GetSymInfoVerbRecord() {
  return VerbRecord(&RunVerbSymInfo, {"sym-info"}, kSymInfoShortHelp, kSymInfoHelp,
                    CommandGroup::kSymbol);
}

}  // namespace zxdb
