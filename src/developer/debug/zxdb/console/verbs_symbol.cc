// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <algorithm>
#include <limits>
#include <set>
#include <vector>

#include "llvm/Demangle/Demangle.h"
#include "src/developer/debug/shared/regex.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_context.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/index.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_status.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"
#include "src/developer/debug/zxdb/symbols/system_symbols.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/ascii.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kListAllSwitch = 1;
constexpr int kListContextSwitch = 2;
constexpr int kListFilePaths = 3;
constexpr int kDumpIndexSwitch = 4;

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

// auth --------------------------------------------------------------------------------------------

const char kAuthShortHelp[] = "auth: Authenticate with a symbol server.";
const char kAuthHelp[] =
    R"(auth [credentials]

  Authenticates with a symbol server. What that meas will depend on the type of
  authentication the sever supports. Run with no arguments to receive
  instructions on how to proceed.

  Must have a valid symbol server noun. See help for sym-server.

Example

  auth my_secret
  sym-server 3 auth some_credential
)";
Err DoAuth(ConsoleContext* context, const Command& cmd) {
  if (cmd.args().size() > 1u) {
    return Err("auth expects exactly one argument.");
  }

  if (!cmd.sym_server())
    return Err("No symbol server selected.");

  if (cmd.sym_server()->state() != SymbolServer::State::kAuth) {
    return Err("Server is not requesting authentication.");
  }

  if (cmd.args().size() == 0) {
    if (cmd.sym_server()->auth_type() != SymbolServer::AuthType::kOAuth) {
      return Err("Unknown authentication type.");
    }

    Console::get()->Output(std::string("To authenticate, please supply an authentication "
                                       "token. You can retrieve a token from:\n\n") +
                           cmd.sym_server()->AuthInfo() +
                           "\n\nOnce you've retrieved a token, run 'auth <token>'");
    return Err();
  }

  cmd.sym_server()->Authenticate(cmd.args()[0], [name = cmd.sym_server()->name()](const Err& err) {
    if (!err.has_error()) {
      Console::get()->Output(std::string("Successfully authenticated with ") + name);
    } else {
      Console::get()->Output(std::string("Authentication with ") + name + " failed: " + err.msg());
    }
  });

  return Err();  // Will complete asynchronously.
}

// list --------------------------------------------------------------------------------------------

const char kListShortHelp[] = "list / l: List source code.";
const char kListHelp[] =
    R"(list [ -a ] [ -c <num_lines> ] [ <location> ]

  Alias: "l"

  Lists source code.

  By default, it will list the source code around the current frame's
  instruction pointer. This can be overridden by supplying an explicit frame,
  or by specifying a symbol or address to list.

  Files are found by taking each path in the "build-dirs" (see "get build-dirs")
  setting and appending the string specified in the symbol file. The first file
  that is found will be used.

Switches

  -a
  --all
      List all lines in the file.

  -c <num_lines>
  --context <num_lines>
      Supply <num_lines> lines of context on each side of the line.

  -f
  --with-filename
      Force the display of file paths at the beginning of the listing. This is
      is equivalent to setting the global option "show-file-paths" for this one
      listing.

Location arguments

)" LOCATION_ARG_HELP("list")
        R"(
Examples

  l
  list
      List around the current frame's location.

  f 2 l
  frame 2 list
      List around frame 2's location.

  list -c 20 Foo
      List 20 lines around the beginning of the given symbol.
)";

// Expands the input file name to a fully qualified one if it is unique. If it's ambiguous, return
// an error.
Err CanonicalizeFile(const TargetSymbols* target_symbols, const FileLine& input, FileLine* output) {
  auto matches = target_symbols->FindFileMatches(input.file());
  if (matches.empty()) {
    // No match.
    return Err("There is no source file in this process matching \"" + input.file() + "\".");
  }

  if (matches.size() == 1) {
    // Unambiguous match.
    *output = FileLine(matches[0], input.line());
    return Err();
  }

  // Non-unique file name, generate a disambiguation error.
  std::string msg("The file name is ambiguous, it could be:\n");
  for (const auto& match : matches)
    msg.append("  " + match + "\n");
  return Err(msg);
}

// target_symbols is required but process_symbols may be null if the process is not running. In that
// case, if a running process is required to resolve the input, an error will be thrown.
Err ParseListLocation(const TargetSymbols* target_symbols, const ProcessSymbols* process_symbols,
                      const Frame* frame, const std::string& arg, FileLine* file_line) {
  // One arg = normal location (ParseInputLocation can handle null frames).
  std::vector<InputLocation> input_locations;
  if (Err err = ParseLocalInputLocation(frame, arg, &input_locations); err.has_error())
    return err;
  FXL_DCHECK(!input_locations.empty());

  // When a file/line is given, we don't actually want to look up the symbol information, just match
  // file names. Then we can find the requested line in the file regardless of whether there's a
  // symbol for it.
  //
  // We can assume file name inputs will only resolve to one InputLocation. Multiple outputs only
  // happens for symbolic names.
  if (input_locations.size() == 1u && input_locations[0].type == InputLocation::Type::kLine)
    return CanonicalizeFile(target_symbols, input_locations[0].line, file_line);

  if (!process_symbols) {
    // This could be enhanced to support listing when there is no running process but there are
    // symbols loaded (the TargetSymbols) should have file names and such). This isn't a big
    // use-case currently and it requires different resolution machinery, so skip for now.
    return Err("Can't list without a currently running process.");
  }

  std::vector<Location> locations;
  if (Err err = ResolveInputLocations(process_symbols, input_locations, true, &locations);
      err.has_error())
    return err;

  // Inlined functions might resolve to many locations, but only one file/line, or there could be
  // multiple file name matches. Find the unique ones.
  std::set<FileLine> matches;
  for (const auto& location : locations) {
    if (location.file_line().is_valid())
      matches.insert(location.file_line());
  }

  // Check for no matches after extracting file/line info in case some matches lacked file/line
  // information.
  if (matches.empty()) {
    if (!locations.empty())
      return Err("The match(es) for this had no line information.");

    // The type won't vary if there are different input locations that were resolved.
    switch (input_locations[0].type) {
      case InputLocation::Type::kLine:
        return Err("There are no files matching \"%s\".", input_locations[0].line.file().c_str());
      case InputLocation::Type::kName:
        return Err("There are no symbols matching \"%s\".",
                   FormatInputLocation(input_locations[0]).AsString().c_str());
      case InputLocation::Type::kAddress:
      case InputLocation::Type::kNone:
      default:
        // Addresses will always be found.
        FXL_NOTREACHED();
        return Err("Internal error.");
    }
  }

  if (matches.size() > 1) {
    std::string msg = "There are multiple matches for this symbol:\n";
    for (const auto& match : matches) {
      msg +=
          fxl::StringPrintf(" %s %s:%d\n", GetBullet().c_str(), match.file().c_str(), match.line());
    }
    return Err(msg);
  }

  *file_line = *matches.begin();
  return Err();
}

Err DoList(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return err;

  // Decode the location. With no argument it uses the frame, with an argument no frame is required.
  FileLine file_line;
  if (cmd.args().empty()) {
    if (!cmd.frame()) {
      return Err(ErrType::kInput, "There isn't a current frame to take the location from.");
    }
    file_line = cmd.frame()->GetLocation().file_line();
  } else if (cmd.args().size() == 1) {
    // Look up some location, depending on the type of input, a running process may or may not be
    // required.
    const ProcessSymbols* process_symbols = nullptr;
    if (cmd.target()->GetProcess())
      process_symbols = cmd.target()->GetProcess()->GetSymbols();

    err = ParseListLocation(cmd.target()->GetSymbols(), process_symbols, cmd.frame(), cmd.args()[0],
                            &file_line);
    if (err.has_error())
      return err;
  } else {
    return Err(ErrType::kInput,
               "Expecting zero or one arg for the location.\n"
               "Formats: <function>, <file>:<line#>, <line#>, or *<address>");
  }

  FormatSourceOpts opts;
  opts.show_file_name =
      cmd.HasSwitch(kListFilePaths) ||
      cmd.target()->session()->system().settings().GetBool(ClientSettings::System::kShowFilePaths);
  opts.highlight_line = file_line.line();

  // Find context amount.
  if (cmd.HasSwitch(kListAllSwitch)) {
    // Full file.
    opts.first_line = 0;
    opts.last_line = std::numeric_limits<int>::max();
  } else if (cmd.HasSwitch(kListContextSwitch)) {
    // Custom context amount.
    int context_lines = 0;
    err = StringToInt(cmd.GetSwitchValue(kListContextSwitch), &context_lines);
    if (err.has_error())
      return err;

    opts.first_line = std::max(0, file_line.line() - context_lines);
    opts.last_line = file_line.line() + context_lines;
  } else {
    // Default context.
    constexpr int kBeforeContext = 5;
    constexpr int kAfterContext = 10;
    opts.first_line = std::max(0, file_line.line() - kBeforeContext);
    opts.last_line = file_line.line() + kAfterContext;
  }

  // When there is a current frame (it's executing), mark the current frame's location so the user
  // can see where things are. This may be different than the symbol looked up which will be
  // highlighted.
  if (cmd.frame()) {
    const FileLine& active_file_line = cmd.frame()->GetLocation().file_line();
    if (active_file_line.file() == file_line.file())
      opts.active_line = active_file_line.line();
  }

  OutputBuffer out;
  err = FormatSourceFileContext(file_line, SourceFileProviderImpl(cmd.target()->settings()), opts,
                                &out);
  if (err.has_error())
    return err;

  Console::get()->Output(out);
  return Err();
}

// sym-info ----------------------------------------------------------------------------------------

const char kSymInfoShortHelp[] = "sym-info: Print information about a symbol.";
const char kSymInfoHelp[] =
    R"(sym-info <name>

  Displays information about a given named symbol.

  It will also show the demangled name if the input is a mangled symbol.

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

Err DoSymInfo(ConsoleContext* context, const Command& cmd) {
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
    find_context = FindNameContext(process_symbols, SymbolContext::ForRelativeAddresses(),
                                   cmd.frame()->GetLocation().symbol().Get()->AsCodeBlock());
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

// sym-stat ----------------------------------------------------------------------------------------

const char kSymStatShortHelp[] = "sym-stat: Print process symbol status.";
const char kSymStatHelp[] =
    R"(sym-stat [ --dump-index ]

  Prints out symbol information.

  With no arguments, this shows global information and information for the
  current (or specified) process. The global information includes the symbol
  search path and how many files are indexed from each location.

  If there is a process it will includes which libraries are loaded, how many
  symbols each has, and where the symbol file is located.

Arguments

  --dump-index
      Dumps the symbol index which maps build IDs to local file paths. This
      can be useful for debugging cases of missing symbols.

Example

  sym-stat

  process 2 sym-stat

  sym-stat --dump-index
)";

void SummarizeProcessSymbolStatus(ConsoleContext* context, Process* process, OutputBuffer* out) {
  // Get modules sorted by name.
  std::vector<ModuleSymbolStatus> modules = process->GetSymbols()->GetStatus();
  std::sort(
      modules.begin(), modules.end(),
      [](const ModuleSymbolStatus& a, const ModuleSymbolStatus& b) { return a.name < b.name; });

  out->Append(Syntax::kHeading, fxl::StringPrintf("\nProcess %d symbol status\n\n",
                                                  context->IdForTarget(process->GetTarget())));

  for (const auto& module : modules) {
    out->Append(Syntax::kHeading, "  " + module.name + "\n");
    out->Append(fxl::StringPrintf("    Base: 0x%" PRIx64 "\n", module.base));
    out->Append("    Build ID: " + module.build_id);

    if (context->session()->system().HasDownload(module.build_id)) {
      out->Append(Syntax::kWarning, " (Downloading...)");
    }

    out->Append("\n");

    if (module.symbols_loaded) {
      out->Append("    Symbols loaded: Yes\n    Symbol file: " + module.symbol_file);
      out->Append(module.files_indexed ? Syntax::kNormal : Syntax::kError,
                  fxl::StringPrintf("\n    Source files indexed: %zu", module.files_indexed));
      out->Append(module.functions_indexed ? Syntax::kNormal : Syntax::kError,
                  fxl::StringPrintf("\n    Symbols indexed: %zu", module.functions_indexed));
    } else {
      out->Append(Syntax::kError, "    Symbols loaded: No");
    }
    out->Append("\n\n");
  }

  if (modules.empty())
    out->Append(Syntax::kError, "  No known modules.\n");

  out->Append(Syntax::kWarning, "  👉 ");
  out->Append(Syntax::kComment, "Use \"libs\" to refresh the module list from the process.");
  out->Append(Syntax::kNormal, "\n\n");
}

void DumpIndexOverview(SystemSymbols* system_symbols, OutputBuffer* out) {
  out->Append(Syntax::kHeading, "Symbol index status\n\n");

  std::vector<std::vector<OutputBuffer>> table;
  auto index_status = system_symbols->build_id_index().GetStatus();
  if (index_status.empty()) {
    out->Append(Syntax::kError, "  No symbol locations are indexed.");
    out->Append(
        "\n\n  Use the command-line switch \"zxdb -s <path>\" or the option \"symbol-paths\"\n"
        "  (see \"get/set symbol-paths\") to specify the location of your symbols.\n\n");
  } else {
    out->Append(Syntax::kComment,
                "  Use \"sym-stat --dump-index\" to see the individual mappings.\n\n");
    for (const auto& pair : index_status) {
      auto& row = table.emplace_back();
      auto syntax = pair.second ? Syntax::kNormal : Syntax::kError;

      if (pair.second != BuildIDIndex::kStatusIsFolder) {
        row.emplace_back(syntax, fxl::StringPrintf("%d", pair.second));
      } else {
        row.emplace_back(syntax, "(folder)");
      }

      row.emplace_back(syntax, pair.first);
    }
    FormatTable(
        {ColSpec(Align::kRight, 0, "Indexed", 2), ColSpec(Align::kLeft, 0, "Source path", 1)},
        table, out);
  }
}

void DumpBuildIdIndex(SystemSymbols* system_symbols, OutputBuffer* out) {
  const auto& build_id_to_files = system_symbols->build_id_index().build_id_to_files();
  if (build_id_to_files.empty()) {
    out->Append(Syntax::kError, "  No build IDs found.\n");
  } else {
    for (const auto& [id, files] : build_id_to_files)
      out->Append(fxl::StringPrintf("%s %s\n", id.c_str(), files.debug_info.c_str()));
  }
  out->Append("\n");
}

Err DoSymStat(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  if (!cmd.args().empty())
    return Err("\"sym-stat\" takes no arguments.");

  SystemSymbols* system_symbols = context->session()->system().GetSymbols();
  OutputBuffer out;

  if (cmd.HasSwitch(kDumpIndexSwitch)) {
    DumpBuildIdIndex(system_symbols, &out);
  } else {
    DumpIndexOverview(system_symbols, &out);

    // Process symbol status (if any).
    if (cmd.target() && cmd.target()->GetProcess())
      SummarizeProcessSymbolStatus(context, cmd.target()->GetProcess(), &out);
  }

  Console* console = Console::get();
  console->Output(out);

  return Err();
}

// sym-near ----------------------------------------------------------------------------------------

const char kSymNearShortHelp[] = "sym-near / sn: Print symbol for an address.";
const char kSymNearHelp[] =
    R"(sym-near <address-expression>

  Alias: "sn"

  Finds the symbol nearest to the given address. This command is useful for
  finding what a pointer or a code location refers to.

  The address can be an explicit number or any expression ("help print") that
  evaluates to a memory address.

Example

  sym-near 0x12345670
  process 2 sym-near &x
)";

Err DoSymNear(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;
  err = AssertRunningTarget(context, "sym-near", cmd.target());
  if (err.has_error())
    return err;

  return EvalCommandAddressExpression(
      cmd, "sym-near", GetEvalContextForCommand(cmd),
      [weak_process = cmd.target()->GetProcess()->GetWeakPtr()](const Err& err, uint64_t address,
                                                                std::optional<uint64_t> size) {
        Console* console = Console::get();
        if (err.has_error()) {
          console->Output(err);  // Evaluation error.
          return;
        }
        if (!weak_process) {
          // Process has been destroyed during evaluation. Normally a message will be printed when
          // that happens so we can skip reporting the error.
          return;
        }

        auto locations = weak_process->GetSymbols()->ResolveInputLocation(InputLocation(address));
        FXL_DCHECK(locations.size() == 1u);

        FormatLocationOptions opts(weak_process->GetTarget());
        opts.always_show_addresses = true;
        opts.show_params = true;
        opts.show_file_line = true;

        console->Output(FormatLocation(locations[0], opts));
      });
}

// sym-search ------------------------------------------------------------------

constexpr size_t kSymSearchListLimit = 200;

constexpr int kSymSearchUnfold = 1;
constexpr int kSymSearchListAll = 2;

const char kSymSearchShortHelp[] = "sym-search: Search for symbols.";
const char kSymSearchHelp[] =
    R"(sym-search [--all] [--unfold] [<regexp>]

  Searches for symbols loaded by a process.

  By default will display all the symbols loaded by the process, truncated to a
  limit. It is possible to use a regular expression to limit the search to a
  desired symbol(s).

  Default display is nested scoping (namespaces, classes) to be joined by "::".
  While this looks similar to what C++ symbols are, they are not meant to be
  literal C++ symbols, but rather to have a relatively familiar way of
  displaying symbols.

  The symbols are displayed by loaded modules.

Arguments

  <regexp>
      Case insensitive regular expression. Uses the POSIX Extended Regular
      Expression syntax. This regexp will be compared with every symbol. Any
      successful matches will be included in the output.

      NOTE: Currently using both regexp and unfold (-u) result in the scoping
            symbols to not be outputted. In order to see the complete scopes,
            don't unfold the output.

  --all | -a
      Don't limit the output. By default zxdb will limit the amount of output
      in order not to print thousands of entries.

  --unfold | -u
      This changes to use a "nesting" formatting, in which scoping symbols,
      such as namespaces or classes, indent other symbols.

Examples

  sym-search
      List all the symbols with the default C++-ish nesting collapsing.

      some_module.so

      nested::scoping::symbol
      nested::scoping::other_symbol
      ...

  pr 3 sym-search other
      Filter using "other" as a regular expression for process 3.

      some_module.so

      nested::scoping::other_symbol
      ...

  sym-search --unfold
      List all the symbols in an unfolded fashion.
      This will be truncated.

      some_module.so

      nested
        scoping
          symbol
          other_symbol
      ...
)";

struct CaseInsensitiveCompare {
  bool operator()(const std::string* lhs, const std::string* rhs) const {
    auto lhs_it = lhs->begin();
    auto rhs_it = rhs->begin();

    while (lhs_it != lhs->end() && rhs_it != rhs->end()) {
      char lhs_low = fxl::ToLowerASCII(*lhs_it);
      char rhs_low = fxl::ToLowerASCII(*rhs_it);
      if (lhs_low != rhs_low)
        return lhs_low < rhs_low;

      lhs_it++;
      rhs_it++;
    }

    // The shortest string wins!
    return lhs->size() < rhs->size();
  }
};

std::string CreateSymbolName(const Command& cmd, const std::vector<std::string>& names,
                             int indent_level) {
  if (cmd.HasSwitch(kSymSearchUnfold))
    return fxl::StringPrintf("%*s%s", indent_level, "", names.back().c_str());
  return fxl::JoinStrings(names, "::");
}

struct DumpModuleContext {
  std::vector<std::string>* names = nullptr;
  std::vector<std::string>* output = nullptr;
  debug_ipc::Regex* regex = nullptr;  // nullptr if no filter is defined.
};

// Returns true if the list was truncated.
bool DumpModule(const Command& cmd, const IndexNode& node, DumpModuleContext* context,
                int indent_level = 0) {
  // Root node doesn't have a name, so it's not printed.
  bool root = context->names->empty();
  if (!root) {
    auto name = CreateSymbolName(cmd, *context->names, indent_level);
    if (!context->regex || context->regex->Match(name)) {
      context->output->push_back(std::move(name));
    }
  }

  if (!cmd.HasSwitch(kSymSearchListAll) && context->output->size() >= kSymSearchListLimit)
    return true;

  // Root should not indent forward.
  indent_level = root ? 0 : indent_level + 2;
  for (int i = 0; i < static_cast<int>(IndexNode::Kind::kEndPhysical); i++) {
    const IndexNode::Map& map = node.MapForKind(static_cast<IndexNode::Kind>(i));
    for (const auto& [child_name, child] : map) {
      context->names->push_back(child_name);
      if (DumpModule(cmd, child, context, indent_level))
        return true;
      context->names->pop_back();
    }
  }

  return false;
}

Err DoSymSearch(ConsoleContext* context, const Command& cmd) {
  if (cmd.args().size() > 1)
    return Err("Too many arguments. See \"help sym-search\".");

  Process* process = cmd.target()->GetProcess();
  if (!process)
    return Err("No process is running.");

  ProcessSymbols* process_symbols = process->GetSymbols();
  auto process_status = process_symbols->GetStatus();

  // We sort them alphabetically in order to ensure all runs return the same
  // result.
  std::sort(process_status.begin(), process_status.end(),
            [](const ModuleSymbolStatus& lhs, const ModuleSymbolStatus& rhs) {
              return lhs.name < rhs.name;
            });

  Console* console = Console::get();

  debug_ipc::Regex regex;
  if (cmd.args().size() == 1) {
    if (!regex.Init(cmd.args().front()))
      return Err("Could not initialize regex %s.", cmd.args().front().c_str());
  }

  // The collected symbols that pass the filter.
  std::vector<std::string> dump;
  // Marks where within the dump vector each module ends.
  std::vector<std::pair<ModuleSymbolStatus, size_t>> module_symbol_indices;
  bool truncated = false;
  for (auto& module_status : process_status) {
    if (!module_status.symbols)
      continue;

    const auto& index = module_status.symbols->module_symbols()->GetIndex();
    const auto& root = index.root();

    std::vector<std::string> names;
    size_t size_before = dump.size();

    DumpModuleContext dump_context;
    dump_context.names = &names;
    dump_context.output = &dump;
    dump_context.regex = regex.valid() ? &regex : nullptr;
    truncated = DumpModule(cmd, root, &dump_context);

    // Only track this module if symbols were actually added.
    if (size_before < dump.size())
      module_symbol_indices.push_back({module_status, dump.size()});
    if (truncated)
      break;
  }

  size_t current_index = 0;
  for (const auto& [module_info, limit] : module_symbol_indices) {
    console->Output(
        OutputBuffer(Syntax::kHeading, fxl::StringPrintf("%s\n\n", module_info.name.c_str())));

    while (current_index < limit) {
      console->Output(dump[current_index]);
      current_index++;
    }
    console->Output("\n");
  }

  if (truncated) {
    console->Output(
        Err("Limiting results to %lu. Make a more specific filter or use "
            "--all.",
            dump.size()));
  } else {
    console->Output(fxl::StringPrintf("Displaying %lu entries.", dump.size()));
  }

  return Err();
}

}  // namespace

void AppendSymbolVerbs(std::map<Verb, VerbRecord>* verbs) {
  VerbRecord list(&DoList, &CompleteInputLocation, {"list", "l"}, kListShortHelp, kListHelp,
                  CommandGroup::kQuery, SourceAffinity::kSource);
  list.switches.emplace_back(kListAllSwitch, false, "all", 'a');
  list.switches.emplace_back(kListContextSwitch, true, "context", 'c');
  list.switches.emplace_back(kListFilePaths, false, "with-filename", 'f');

  (*verbs)[Verb::kList] = std::move(list);
  (*verbs)[Verb::kSymInfo] =
      VerbRecord(&DoSymInfo, {"sym-info"}, kSymInfoShortHelp, kSymInfoHelp, CommandGroup::kSymbol);

  // sym-stat
  VerbRecord sym_stat(&DoSymStat, {"sym-stat"}, kSymStatShortHelp, kSymStatHelp,
                      CommandGroup::kSymbol);
  sym_stat.switches.emplace_back(kDumpIndexSwitch, false, "dump-index", 0);
  (*verbs)[Verb::kSymStat] = std::move(sym_stat);

  // sym-near
  VerbRecord sym_near(&DoSymNear, {"sym-near", "sn"}, kSymNearShortHelp, kSymNearHelp,
                      CommandGroup::kSymbol);
  sym_near.param_type = VerbRecord::kOneParam;
  (*verbs)[Verb::kSymNear] = std::move(sym_near);

  VerbRecord search(&DoSymSearch, {"sym-search"}, kSymSearchShortHelp, kSymSearchHelp,
                    CommandGroup::kSymbol);
  search.switches.emplace_back(kSymSearchListAll, false, "--all", 'a');
  search.switches.emplace_back(kSymSearchUnfold, false, "unfold", 'u');
  (*verbs)[Verb::kSymSearch] = std::move(search);
  (*verbs)[Verb::kAuth] =
      VerbRecord(&DoAuth, {"auth"}, kAuthShortHelp, kAuthHelp, CommandGroup::kSymbol);
}

}  // namespace zxdb
