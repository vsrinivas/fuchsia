// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_sym_stat.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_status.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/system_symbols.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kDumpIndexSwitch = 1;

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
        row.emplace_back(syntax, std::to_string(pair.second));
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

Err RunVerbSymStat(ConsoleContext* context, const Command& cmd) {
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

}  // namespace

VerbRecord GetSymStatVerbRecord() {
  VerbRecord sym_stat(&RunVerbSymStat, {"sym-stat"}, kSymStatShortHelp, kSymStatHelp,
                      CommandGroup::kSymbol);
  sym_stat.switches.emplace_back(kDumpIndexSwitch, false, "dump-index", 0);
  return sym_stat;
}

}  // namespace zxdb
