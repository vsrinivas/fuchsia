// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_list.h"

#include <set>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_context.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kListAllSwitch = 1;
constexpr int kListContextSwitch = 2;
constexpr int kListFilePaths = 3;

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
    *output = FileLine(matches[0].first, matches[0].second, input.line());
    return Err();
  }

  // Non-unique file name, generate a disambiguation error.
  std::string msg("The file name is ambiguous, it could be:\n");
  for (const auto& match : matches) {
    if (match.second.empty()) {
      msg.append("  " + match.first + "\n");
    } else {
      msg.append("  " + match.first + " (from " + match.second + ")\n");
    }
  }
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
  FX_DCHECK(!input_locations.empty());

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
        FX_NOTREACHED();
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

void RunVerbList(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return cmd_context->ReportError(err);

  FormatSourceOpts opts;

  // Decode the location. With no argument it uses the frame, with an argument no frame is required.
  FileLine file_line;
  if (cmd.args().empty()) {
    if (!cmd.frame()) {
      return cmd_context->ReportError(
          Err(ErrType::kInput, "There isn't a current frame to take the location from."));
    }
    const Location& loc = cmd.frame()->GetLocation();
    file_line = loc.file_line();

    // Extract the language of the current symbol for highlighting.
    if (const Symbol* sym = loc.symbol().Get())
      opts.language = DwarfLangToExprLanguage(sym->GetLanguage());

  } else if (cmd.args().size() == 1) {
    // Look up some location, depending on the type of input, a running process may or may not be
    // required.
    const ProcessSymbols* process_symbols = nullptr;
    if (cmd.target()->GetProcess())
      process_symbols = cmd.target()->GetProcess()->GetSymbols();

    err = ParseListLocation(cmd.target()->GetSymbols(), process_symbols, cmd.frame(), cmd.args()[0],
                            &file_line);
    if (err.has_error())
      return cmd_context->ReportError(err);
  } else {
    return cmd_context->ReportError(
        Err(ErrType::kInput,
            "Expecting zero or one arg for the location.\n"
            "Formats: <function>, <file>:<line#>, <line#>, or 0x<address>"));
  }

  if (!opts.language) {
    // Autodetect the language for anything that doesn't have a language from the symbols.
    opts.SetLanguageFromFileName(file_line.file());
  }

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
      return cmd_context->ReportError(err);

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
    return cmd_context->ReportError(err);

  cmd_context->Output(out);
}

}  // namespace

VerbRecord GetListVerbRecord() {
  VerbRecord list(&RunVerbList, &CompleteInputLocation, {"list", "l"}, kListShortHelp, kListHelp,
                  CommandGroup::kQuery, SourceAffinity::kSource);
  list.switches.emplace_back(kListAllSwitch, false, "all", 'a');
  list.switches.emplace_back(kListContextSwitch, true, "context", 'c');
  list.switches.emplace_back(kListFilePaths, false, "with-filename", 'f');
  return list;
}

}  // namespace zxdb
