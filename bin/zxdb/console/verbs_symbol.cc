// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>
#include <algorithm>
#include <limits>
#include <vector>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/symbols/module_symbol_status.h"
#include "garnet/bin/zxdb/client/symbols/process_symbols.h"
#include "garnet/bin/zxdb/client/symbols/target_symbols.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/format_context.h"
#include "garnet/bin/zxdb/console/input_location_parser.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kListAllSwitch = 1;
constexpr int kListContextSwitch = 2;

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
      List around the current frame's locaton.

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

Err ResolveSymbol(const TargetSymbols* target_symbols,
                  const std::string& symbol, FileLine* output) {
  auto matches = target_symbols->FindLinesForSymbol(symbol);
  if (matches.empty()) {
    // No match.
    return Err("There are no symbols matching \"" + symbol +
               "\" in this process.");
  }

  if (matches.size() == 1) {
    // Unambiguous match.
    *output = std::move(matches.front());
    return Err();
  }

  // Ambiguous match, generate disambiguation error.
  std::string msg("The symbol is ambiguous, it could be:\n");
  for (const auto& match : matches)
    msg.append("  " + DescribeFileLine(match, false) + "\n");
  return Err(msg);
}

Err ResolveAddress(const ProcessSymbols* process_symbols, uint64_t address,
                   FileLine* file_line) {
  Location location = process_symbols->LocationForAddress(address);
  if (!location.has_symbols())
    return Err("There is no source information for this address.");

  *file_line = location.file_line();
  return Err();
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

  switch (input_location.type) {
    case InputLocation::Type::kLine:
      return CanonicalizeFile(target_symbols, input_location.line, file_line);
    case InputLocation::Type::kSymbol:
      return ResolveSymbol(target_symbols, input_location.symbol, file_line);
    case InputLocation::Type::kAddress:
      if (!process_symbols)
        return Err("Looking up an address requires a running process.");
      return ResolveAddress(process_symbols, input_location.address, file_line);
    case InputLocation::Type::kNone:
      break;
  }
  return Err("Invalid input location.");
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

  Console::get()->Output(std::move(out));
  return Err();
}

// sym-stat --------------------------------------------------------------------

const char kSymStatShortHelp[] = "sym-stat: Print process symbol status.";
const char kSymStatHelp[] =
    R"(sym-stat

  Prints out the symbol information for the current process.

Example

  sym-stat
  process 2 sym-stat
)";

Err DoSymStat(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;
  err = AssertRunningTarget(context, "sym-stat", cmd.target());
  if (err.has_error())
    return err;

  if (!cmd.args().empty())
    return Err("\"sym-stat\" takes no arguments.");

  std::string load_tip(
      "Tip: Use \"libs\" to refresh the module list from the process.\n");

  std::vector<ModuleSymbolStatus> modules =
      cmd.target()->GetProcess()->GetSymbols()->GetStatus();

  Console* console = Console::get();
  if (modules.empty()) {
    console->Output("No known modules.\n" + load_tip);
    return Err();
  }

  // Sort by name.
  std::sort(modules.begin(), modules.end(),
            [](const ModuleSymbolStatus& a, const ModuleSymbolStatus& b) {
              return a.name < b.name;
            });

  OutputBuffer out;
  for (const auto& module : modules) {
    out.Append(Syntax::kHeading, module.name + "\n");
    out.Append(fxl::StringPrintf("  Base: 0x%" PRIx64 "\n", module.base));
    out.Append("  Build ID: " + module.build_id + "\n");

    out.Append("  Symbols loaded: ");
    if (module.symbols_loaded) {
      out.Append("Yes\n  Symbol file: " + module.symbol_file);
    } else {
      out.Append(Syntax::kError, "No");
    }

    out.Append(fxl::StringPrintf(
        "\n  Source files indexed: %zu\n  Symbols indexed: %zu\n\n",
        module.files_indexed, module.functions_indexed));
  }
  out.Append(std::move(load_tip));
  console->Output(std::move(out));

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

  Location loc =
      cmd.target()->GetProcess()->GetSymbols()->LocationForAddress(address);
  Console::get()->Output(DescribeLocation(loc, true));
  return Err();
}

}  // namespace

void AppendSymbolVerbs(std::map<Verb, VerbRecord>* verbs) {
  VerbRecord list(&DoList, {"list", "l"}, kListShortHelp, kListHelp,
                  CommandGroup::kQuery, SourceAffinity::kSource);
  list.switches.emplace_back(kListAllSwitch, false, "all", 'a');
  list.switches.emplace_back(kListContextSwitch, true, "context", 'c');

  (*verbs)[Verb::kList] = std::move(list);
  (*verbs)[Verb::kSymStat] =
      VerbRecord(&DoSymStat, {"sym-stat"}, kSymStatShortHelp, kSymStatHelp,
                 CommandGroup::kQuery);
  (*verbs)[Verb::kSymNear] =
      VerbRecord(&DoSymNear, {"sym-near", "sn"}, kSymNearShortHelp,
                 kSymNearHelp, CommandGroup::kQuery);
}

}  // namespace zxdb
