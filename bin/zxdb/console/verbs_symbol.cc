// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>
#include <algorithm>
#include <vector>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/symbols.h"
#include "garnet/bin/zxdb/client/symbols/module_records.h"
#include "garnet/bin/zxdb/client/symbols/symbol.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// sym-info --------------------------------------------------------------------

const char kSymInfoShortHelp[] = "sym-info: Print process symbol information.";
const char kSymInfoHelp[] =
    R"(sym-info

  Prints out the symbol information for the current process.

Example

  sym-info
  process 2 sym-info
)";

Err DoSymInfo(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;
  err = AssertRunningTarget(context, "sym-info", cmd.target());
  if (err.has_error())
    return err;

  if (!cmd.args().empty())
    return Err("\"sym-info\" takes no arguments.");

  cmd.target()->GetProcess()->GetSymbols()->GetModuleInfo(
      [](std::vector<ModuleSymbolRecord> records) {
        std::string load_tip(
            "Tip: Use \"libs\" to refresh the module list from the process.\n");

        Console* console = Console::get();
        if (records.empty()) {
          console->Output("No known modules.\n" + load_tip);
          return;
        }

        // Sort by target module name.
        std::sort(records.begin(), records.end(),
                  [](const ModuleSymbolRecord& a, const ModuleSymbolRecord& b) {
                    return a.module_name < b.module_name;
                  });

        OutputBuffer out;
        for (const auto& module : records) {
          out.Append(Syntax::kHeading, module.module_name + "\n");
          out.Append(fxl::StringPrintf("  Base: 0x%" PRIx64 "\n", module.base));
          out.Append("  Build ID: " + module.build_id + "\n");
          out.Append("  Local file: ");
          if (module.local_path.empty())
            out.Append(Syntax::kError, "Not found.");
          else
            out.Append(module.local_path);
          out.Append("\n\n");
        }
        out.Append(std::move(load_tip));
        console->Output(std::move(out));
      });
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

  cmd.target()->GetProcess()->GetSymbols()->SymbolAtAddress(
      address,
      [](Symbol symbol) { Console::get()->Output(DescribeSymbol(symbol)); });
  return Err();
}

}  // namespace

void AppendSymbolVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kSymInfo] =
      VerbRecord(&DoSymInfo, {"sym-info"}, kSymInfoShortHelp, kSymInfoHelp);
  (*verbs)[Verb::kSymNear] = VerbRecord(&DoSymNear, {"sym-near", "sn"},
                                        kSymNearShortHelp, kSymNearHelp);
}

}  // namespace zxdb
