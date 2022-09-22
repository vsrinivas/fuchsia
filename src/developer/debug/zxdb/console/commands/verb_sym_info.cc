// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_sym_info.h"

#include "llvm/Demangle/Demangle.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/console/format_symbol.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kDwarfExprSwitch = 1;

const char kSymInfoShortHelp[] = "sym-info: Print information about a symbol.";
const char kSymInfoHelp[] =
    R"(sym-info <name>

  Displays information about a given named symbol.

  It will also show the demangled name if the input is a mangled symbol.

Options

)" DWARF_EXPR_COMMAND_SWTICH_HELP
    R"(
Example

  sym-info i
  thread 1 frame 4 sym-info i
)";

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

  return llvm::demangle(full_input);
}

void RunVerbSymInfo(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (cmd.args().empty())
    return cmd_context->ReportError(Err("sym-info expects the name of the symbol to look up."));

  ParsedIdentifier identifier;
  if (Err err = ExprParser::ParseIdentifier(cmd.args()[0], &identifier); err.has_error())
    return cmd_context->ReportError(err);

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
    process_symbols = cmd.target()->GetProcess()->GetSymbols();
    if (cmd.frame()) {
      find_context = cmd.frame()->GetEvalContext()->GetFindNameContext();
    } else {
      // This command can support querying symbols without a current stack frame, in which case it
      // will do a global search of names in the current process.
      find_context = FindNameContext(process_symbols);
    }
  } else {
    // Non-running process. Can do some lookup for some things.
    find_context = FindNameContext(cmd.target()->GetSymbols());
  }

  FindNameOptions find_opts(FindNameOptions::kAllKinds);
  find_opts.max_results = std::numeric_limits<size_t>::max();

  std::vector<FoundName> found_items;
  FindName(find_context, find_opts, identifier, &found_items);

  ErrOr<FormatSymbolOptions> opts = GetFormatSymbolOptionsFromCommand(cmd, kDwarfExprSwitch);
  if (opts.has_error())
    return cmd_context->ReportError(opts.err());

  bool found_item = false;
  for (const FoundName& found : found_items) {
    switch (found.kind()) {
      case FoundName::kNone:
        continue;
      case FoundName::kVariable:
        out.Append(FormatSymbol(process_symbols, found.variable(), opts.value()));
        break;
      case FoundName::kMemberVariable:
        out.Append(FormatSymbol(process_symbols, found.member().data_member(), opts.value()));
        break;
      case FoundName::kNamespace:
        // Probably useless to display info on a namespace.
        continue;
      case FoundName::kTemplate:
        // TODO(brettw) it would be nice to list all template specializations here.
        continue;
      case FoundName::kType:
        out.Append(FormatSymbol(process_symbols, found.type().get(), opts.value()));
        break;
      case FoundName::kFunction:
        out.Append(FormatSymbol(process_symbols, found.function().get(), opts.value()));
        break;
      case FoundName::kOtherSymbol:
        out.Append(FormatSymbol(process_symbols, found.other_symbol().get(), opts.value()));
        break;
    }
    found_item = true;
    out.Append("\n");
  }

  if (!found_item) {
    out.Append("No symbol \"");
    out.Append(FormatIdentifier(identifier, FormatIdentifierOptions()));
    out.Append("\" found in the current context.\n");
  }
  if (!out.empty())
    cmd_context->Output(out);
}

}  // namespace

VerbRecord GetSymInfoVerbRecord() {
  VerbRecord sym_info(&RunVerbSymInfo, {"sym-info"}, kSymInfoShortHelp, kSymInfoHelp,
                      CommandGroup::kSymbol);

  SwitchRecord dwarf_expr_switch(kDwarfExprSwitch, true, "dwarf-expr");
  sym_info.switches = {dwarf_expr_switch};

  // Accept just one arg and allow for spaces in it.
  sym_info.param_type = VerbRecord::kOneParam;

  return sym_info;
}

}  // namespace zxdb
