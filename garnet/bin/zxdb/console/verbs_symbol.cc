// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>
#include <algorithm>
#include <limits>
#include <set>
#include <vector>

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/format_context.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/input_location_parser.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_util.h"
#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/identifier.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/loaded_module_symbols.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/module_symbol_index.h"
#include "garnet/bin/zxdb/symbols/module_symbol_status.h"
#include "garnet/bin/zxdb/symbols/module_symbols.h"
#include "garnet/bin/zxdb/symbols/process_symbols.h"
#include "garnet/bin/zxdb/symbols/resolve_options.h"
#include "garnet/bin/zxdb/symbols/system_symbols.h"
#include "garnet/bin/zxdb/symbols/target_symbols.h"
#include "garnet/bin/zxdb/symbols/type.h"
#include "garnet/bin/zxdb/symbols/variable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/ascii.h"
#include "lib/fxl/strings/join_strings.h"
#include "lib/fxl/strings/string_printf.h"
#include "src/developer/debug/shared/regex.h"

namespace zxdb {

namespace {

constexpr int kListAllSwitch = 1;
constexpr int kListContextSwitch = 2;
constexpr int kDumpIndexSwitch = 3;

void DumpVariableLocation(const SymbolContext& symbol_context,
                          const VariableLocation& loc, OutputBuffer* out) {
  if (loc.is_null()) {
    out->Append("DWARF location: <no location info>\n");
    return;
  }

  out->Append("DWARF location (address range + DWARF expression bytes):\n");
  for (const auto& entry : loc.locations()) {
    // Address range.
    if (entry.begin == 0 && entry.end == 0) {
      out->Append("  <always valid>:");
    } else {
      out->Append(
          fxl::StringPrintf("  [0x%" PRIx64 ", 0x%" PRIx64 "):",
                            symbol_context.RelativeToAbsolute(entry.begin),
                            symbol_context.RelativeToAbsolute(entry.end)));
    }

    // Dump the raw DWARF expression bytes. In the future we can decode if
    // necessary (check LLVM's "dwarfdump" utility which can do this).
    for (uint8_t byte : entry.expression)
      out->Append(fxl::StringPrintf(" 0x%02x", byte));
    out->Append("\n");
  }
}

std::string GetTypeDescription(const LazySymbol& lazy_type) {
  if (const Type* type = lazy_type.Get()->AsType())
    return type->GetFullName();
  return "<bad type>";
}

void DumpVariableInfo(const SymbolContext& symbol_context,
                      const Variable* variable, OutputBuffer* out) {
  out->Append("Variable: ");
  out->Append(Syntax::kVariable, variable->GetAssignedName());
  out->Append("\n");
  out->Append(fxl::StringPrintf("Type: %s\n",
                                GetTypeDescription(variable->type()).c_str()));
  out->Append(fxl::StringPrintf("DWARF tag: 0x%x\n",
                                static_cast<unsigned>(variable->tag())));
  DumpVariableLocation(symbol_context, variable->location(), out);
}

void DumpDataMemberInfo(const DataMember* data_member, OutputBuffer* out) {
  out->Append("Data member: " + data_member->GetFullName() + "\n");
  const Symbol* parent = data_member->parent().Get();
  out->Append("Contained in: " + parent->GetFullName() + "\n");
  out->Append(fxl::StringPrintf(
      "Type: %s\n", GetTypeDescription(data_member->type()).c_str()));
  out->Append(fxl::StringPrintf("Offset within container: %" PRIu32 "\n",
                                data_member->member_location()));
  out->Append(fxl::StringPrintf("DWARF tag: 0x%x\n",
                                static_cast<unsigned>(data_member->tag())));
}

// list ------------------------------------------------------------------------

const char kListShortHelp[] = "list / l: List source code.";
const char kListHelp[] =
    R"(list [ -a ] [ -c <num_lines> ] [ <location> ]

  Alias: "l"

  Lists source code.

  By default, it will list the source code around the current frame's
  instruction pointer. This can be overridden by supplying an explicit frame,
  or by specifying a symbol or address to list.

Switches

  --all | -a
      List all lines in the file.

  --context <num_lines> | -c <num_lines>
      Supply <num_lines> lines of context on each side of the line.

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

// Expands the input file name to a fully qualified one if it is unique. If
// it's ambiguous, return an error.
Err CanonicalizeFile(const TargetSymbols* target_symbols, const FileLine& input,
                     FileLine* output) {
  auto matches = target_symbols->FindFileMatches(input.file());
  if (matches.empty()) {
    // No match.
    return Err("There is no source file in this process matching \"" +
               input.file() + "\".");
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

// target_symbols is required but process_symbols may be null if the process
// is not running. In that case, if a running process is required to resolve
// the input, an error will be thrown.
Err ParseListLocation(const TargetSymbols* target_symbols,
                      const ProcessSymbols* process_symbols, const Frame* frame,
                      const std::string& arg, FileLine* file_line) {
  // One arg = normal location (ParseInputLocation can handle null frames).
  InputLocation input_location;
  Err err = ParseInputLocation(frame, arg, &input_location);
  if (err.has_error())
    return err;

  // When a file/line is given, we don't actually want to look up the symbol
  // information, just match file names. Then we can find the requested line
  // in the file regardless of whether there's a symbol for it.
  if (input_location.type == InputLocation::Type::kLine)
    return CanonicalizeFile(target_symbols, input_location.line, file_line);

  // Address lookups require a running process, everything else can be done
  // without a process as long as the symbols are loaded (the Target has them).
  std::vector<Location> locations;
  if (input_location.type == InputLocation::Type::kAddress) {
    if (!process_symbols)
      return Err("Looking up an address requires a running process.");
    locations =
        process_symbols->ResolveInputLocation(input_location, ResolveOptions());
  } else {
    locations =
        target_symbols->ResolveInputLocation(input_location, ResolveOptions());
  }

  // Inlined functions might resolve to many locations, but only one file/line,
  // or there could be multiple file name matches. Find the unique ones.
  std::set<FileLine> matches;
  for (const auto& location : locations) {
    if (location.file_line().is_valid())
      matches.insert(location.file_line());
  }

  // Check for no matches after extracting file/line info in case some matches
  // lacked file/line information.
  if (matches.empty()) {
    if (!locations.empty())
      return Err("The match(es) for this had no line information.");

    switch (input_location.type) {
      case InputLocation::Type::kLine:
        return Err("There are no files matching \"%s\".",
                   input_location.line.file().c_str());
      case InputLocation::Type::kSymbol:
        return Err("There are no symbols matching \"%s\".",
                   input_location.symbol.c_str());
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
      msg += fxl::StringPrintf(" %s %s:%d\n", GetBullet().c_str(),
                               match.file().c_str(), match.line());
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

  // Decode the location. With no argument it uses the frame, with an argument
  // no frame is required.
  FileLine file_line;
  if (cmd.args().empty()) {
    if (!cmd.frame()) {
      return Err(ErrType::kInput,
                 "There isn't a current frame to take the location from.");
    }
    file_line = cmd.frame()->GetLocation().file_line();
  } else if (cmd.args().size() == 1) {
    // Look up some location, depending on the type of input, a running process
    // may or may not be required.
    const ProcessSymbols* process_symbols = nullptr;
    if (cmd.target()->GetProcess())
      process_symbols = cmd.target()->GetProcess()->GetSymbols();

    err = ParseListLocation(cmd.target()->GetSymbols(), process_symbols,
                            cmd.frame(), cmd.args()[0], &file_line);
    if (err.has_error())
      return err;
  } else {
    return Err(ErrType::kInput,
               "Expecting zero or one arg for the location.\n"
               "Formats: <function>, <file>:<line#>, <line#>, or *<address>");
  }

  FormatSourceOpts opts;
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

  // When there is a current frame (it's executing), mark the current
  // frame's location so the user can see where things are. This may be
  // different than the symbol looked up which will be highlighted.
  if (cmd.frame()) {
    const FileLine& active_file_line = cmd.frame()->GetLocation().file_line();
    if (active_file_line.file() == file_line.file())
      opts.active_line = active_file_line.line();
  }

  const std::string& build_dir =
      cmd.target()->session()->system().GetSymbols()->build_dir();

  OutputBuffer out;
  err = FormatSourceFileContext(file_line.file(), build_dir, opts, &out);
  if (err.has_error())
    return err;

  Console::get()->Output(out);
  return Err();
}

// sym-info --------------------------------------------------------------------

const char kSymInfoShortHelp[] = "sym-info: Print information about a symbol.";
const char kSymInfoHelp[] =
    R"(sym-info

  Displays information about a given named symbol.

  Currently this only shows information for variables (as that might appear in
  an expression).

  It should be expanded in the future to support global variables and functions
  as well.

Example

  sym-info i
  thread 1 frame 4 sym-info i
)";
Err DoSymInfo(ConsoleContext* context, const Command& cmd) {
  if (cmd.args().size() != 1u) {
    return Err(
        "sym-info expects exactly one argument that's the name of the "
        "symbol to look up.");
  }

  auto [err, identifier] = Identifier::FromString(cmd.args()[0]);
  if (err.has_error())
    return err;

  if (cmd.frame()) {
    const Location& location = cmd.frame()->GetLocation();
    fxl::RefPtr<ExprEvalContext> eval_context =
        cmd.frame()->GetExprEvalContext();
    eval_context->GetNamedValue(
        identifier, [location](const Err& err, fxl::RefPtr<Symbol> symbol,
                               ExprValue value) {
          // Expression evaluation could fail but there still could be a symbol.
          OutputBuffer out;
          if (!symbol) {
            FXL_DCHECK(err.has_error());
            out.Append(err);
          } else if (auto variable = symbol->AsVariable()) {
            DumpVariableInfo(location.symbol_context(), variable, &out);
          } else if (auto data_member = symbol->AsDataMember()) {
            DumpDataMemberInfo(data_member, &out);
          } else {
            out.Append("TODO: support this command for non-Variables.");
          }
          Console::get()->Output(out);
          return Err();
        });
    return Err();  // Will complete asynchronously.
  }

  return Err(fxl::StringPrintf("No symbol \"%s\" found in the current context.",
                               identifier.GetFullName().c_str()));
}

// sym-stat --------------------------------------------------------------------

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

void SummarizeProcessSymbolStatus(ConsoleContext* context, Process* process,
                                  OutputBuffer* out) {
  // Get modules sorted by name.
  std::vector<ModuleSymbolStatus> modules = process->GetSymbols()->GetStatus();
  std::sort(modules.begin(), modules.end(),
            [](const ModuleSymbolStatus& a, const ModuleSymbolStatus& b) {
              return a.name < b.name;
            });

  out->Append(Syntax::kHeading,
              fxl::StringPrintf("\nProcess %d symbol status\n\n",
                                context->IdForTarget(process->GetTarget())));

  for (const auto& module : modules) {
    out->Append(Syntax::kHeading, "  " + module.name + "\n");
    out->Append(fxl::StringPrintf("    Base: 0x%" PRIx64 "\n", module.base));
    out->Append("    Build ID: " + module.build_id + "\n");

    if (module.symbols_loaded) {
      out->Append("    Symbols loaded: Yes\n    Symbol file: " +
                  module.symbol_file);
      out->Append(module.files_indexed ? Syntax::kNormal : Syntax::kError,
                  fxl::StringPrintf("\n    Source files indexed: %zu",
                                    module.files_indexed));
      out->Append(module.functions_indexed ? Syntax::kNormal : Syntax::kError,
                  fxl::StringPrintf("\n    Symbols indexed: %zu",
                                    module.functions_indexed));
    } else {
      out->Append(Syntax::kError, "    Symbols loaded: No");
    }
    out->Append("\n\n");
  }

  if (modules.empty())
    out->Append(Syntax::kError, "  No known modules.\n");

  out->Append(Syntax::kWarning, "  👉 ");
  out->Append(Syntax::kComment,
              "Use \"libs\" to refresh the module list from the process.");
  out->Append(Syntax::kNormal, "\n\n");
}

void DumpIndexOverview(SystemSymbols* system_symbols, OutputBuffer* out) {
  out->Append(Syntax::kHeading, "Symbol index status\n\n");

  std::vector<std::vector<OutputBuffer>> table;
  auto index_status = system_symbols->build_id_index().GetStatus();
  if (index_status.empty()) {
    out->Append(Syntax::kError, "  No symbol locations are indexed.");
    out->Append(
        "\n\n  Use the command-line switch \"zxdb -s <path>\" to "
        "specify the location of\n  your symbols.\n\n");
  } else {
    out->Append(
        Syntax::kComment,
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
    FormatTable({ColSpec(Align::kRight, 0, "Indexed", 2),
                 ColSpec(Align::kLeft, 0, "Source path", 1)},
                table, out);
  }
}

void DumpBuildIdIndex(SystemSymbols* system_symbols, OutputBuffer* out) {
  const auto& build_id_to_file =
      system_symbols->build_id_index().build_id_to_file();
  if (build_id_to_file.empty()) {
    out->Append(Syntax::kError, "  No build IDs found.\n");
  } else {
    for (const auto& [id, file] : build_id_to_file)
      out->Append(fxl::StringPrintf("%s %s\n", id.c_str(), file.c_str()));
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

// sym-near --------------------------------------------------------------------

const char kSymNearShortHelp[] = "sym-near / sn: Print symbol for an address.";
const char kSymNearHelp[] =
    R"(sym-near <address>

  Alias: "sn"

  Finds the symbol nearest to the given address. This command is useful for
  finding what a pointer or a code location refers to.

Example

  sym-near 0x12345670
  process 2 sym-near 0x612a2519
)";

Err DoSymNear(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;
  err = AssertRunningTarget(context, "sym-near", cmd.target());
  if (err.has_error())
    return err;

  if (cmd.args().size() != 1u) {
    return Err(
        ErrType::kInput,
        "\"sym-near\" needs exactly one arg that's the address to lookup.");
  }

  uint64_t address = 0;
  err = StringToUint64(cmd.args()[0], &address);
  if (err.has_error())
    return err;

  auto locations =
      cmd.target()->GetProcess()->GetSymbols()->ResolveInputLocation(
          InputLocation(address));
  FXL_DCHECK(locations.size() == 1u);
  Console::get()->Output(FormatLocation(locations[0], true, true));
  return Err();
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

std::string CreateSymbolName(const Command& cmd,
                             const std::vector<std::string>& names,
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
bool DumpModule(const Command& cmd, const ModuleSymbolIndexNode& node,
                DumpModuleContext* context, int indent_level = 0) {
  // Root node doesn't have a name, so it's not printed.
  bool root = context->names->empty();
  if (!root) {
    auto name = CreateSymbolName(cmd, *context->names, indent_level);
    if (!context->regex || context->regex->Match(name)) {
      context->output->push_back(std::move(name));
    }
  }

  if (!cmd.HasSwitch(kSymSearchListAll) &&
      context->output->size() >= kSymSearchListLimit) {
    return true;
  }

  // Root should not indent forward.
  indent_level = root ? 0 : indent_level + 2;
  for (const auto& [child_name, child] : node.sub()) {
    context->names->push_back(child_name);
    if (DumpModule(cmd, child, context, indent_level))
      return true;
    context->names->pop_back();
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
        OutputBuffer(Syntax::kHeading,
                     fxl::StringPrintf("%s\n\n", module_info.name.c_str())));

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
  VerbRecord list(&DoList, {"list", "l"}, kListShortHelp, kListHelp,
                  CommandGroup::kQuery, SourceAffinity::kSource);
  list.switches.emplace_back(kListAllSwitch, false, "all", 'a');
  list.switches.emplace_back(kListContextSwitch, true, "context", 'c');

  (*verbs)[Verb::kList] = std::move(list);
  (*verbs)[Verb::kSymInfo] =
      VerbRecord(&DoSymInfo, {"sym-info"}, kSymInfoShortHelp, kSymInfoHelp,
                 CommandGroup::kQuery);

  // sym-stat
  VerbRecord sym_stat(&DoSymStat, {"sym-stat"}, kSymStatShortHelp, kSymStatHelp,
                      CommandGroup::kQuery);
  sym_stat.switches.emplace_back(kDumpIndexSwitch, false, "dump-index", 0);
  (*verbs)[Verb::kSymStat] = std::move(sym_stat);

  (*verbs)[Verb::kSymNear] =
      VerbRecord(&DoSymNear, {"sym-near", "sn"}, kSymNearShortHelp,
                 kSymNearHelp, CommandGroup::kQuery);

  VerbRecord search(&DoSymSearch, {"sym-search"}, kSymSearchShortHelp,
                    kSymSearchHelp, CommandGroup::kQuery);
  search.switches.emplace_back(kSymSearchListAll, false, "--all", 'a');
  search.switches.emplace_back(kSymSearchUnfold, false, "unfold", 'u');
  (*verbs)[Verb::kSymSearch] = std::move(search);
}

}  // namespace zxdb
