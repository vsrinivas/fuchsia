// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>
#include <algorithm>
#include <vector>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/symbols/process_symbols.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

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

  std::vector<ProcessSymbols::ModuleStatus> modules =
      cmd.target()->GetProcess()->GetSymbols()->GetStatus();

  Console* console = Console::get();
  if (modules.empty()) {
    console->Output("No known modules.\n" + load_tip);
    return Err();
  }

  // Sort by name.
  std::sort(
      modules.begin(), modules.end(),
      [](const ProcessSymbols::ModuleStatus& a,
         const ProcessSymbols::ModuleStatus& b) { return a.name < b.name; });

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
    out.Append("\n\n");
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
  (*verbs)[Verb::kSymStat] =
      VerbRecord(&DoSymStat, {"sym-stat"}, kSymStatShortHelp, kSymStatHelp,
                 CommandGroup::kQuery);
  (*verbs)[Verb::kSymNear] =
      VerbRecord(&DoSymNear, {"sym-near", "sn"}, kSymNearShortHelp,
                 kSymNearHelp, CommandGroup::kQuery);
}

}  // namespace zxdb
