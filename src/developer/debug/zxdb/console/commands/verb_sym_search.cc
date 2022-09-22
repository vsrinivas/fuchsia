// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_sym_search.h"

#include "src/developer/debug/shared/regex.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/index.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_status.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/lib/fxl/strings/ascii.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

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
  debug::Regex* regex = nullptr;  // nullptr if no filter is defined.
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

void RunVerbSymSearch(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (cmd.args().size() > 1)
    return cmd_context->ReportError(Err("Too many arguments. See \"help sym-search\"."));

  Process* process = cmd.target()->GetProcess();
  if (!process)
    return cmd_context->ReportError(Err("No process is running."));

  ProcessSymbols* process_symbols = process->GetSymbols();
  auto process_status = process_symbols->GetStatus();

  // We sort them alphabetically in order to ensure all runs return the same
  // result.
  std::sort(process_status.begin(), process_status.end(),
            [](const ModuleSymbolStatus& lhs, const ModuleSymbolStatus& rhs) {
              return lhs.name < rhs.name;
            });

  debug::Regex regex;
  if (cmd.args().size() == 1) {
    if (!regex.Init(cmd.args().front())) {
      return cmd_context->ReportError(
          Err("Could not initialize regex %s.", cmd.args().front().c_str()));
    }
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
    cmd_context->Output(
        OutputBuffer(Syntax::kHeading, fxl::StringPrintf("%s\n\n", module_info.name.c_str())));

    while (current_index < limit) {
      cmd_context->Output(dump[current_index]);
      current_index++;
    }
    cmd_context->Output("\n");
  }

  if (truncated) {
    cmd_context->Output(fxl::StringPrintf(
        "Limiting results to %lu. Make a more specific filter or use --all.", dump.size()));
  } else {
    cmd_context->Output(fxl::StringPrintf("Displaying %zu entries.", dump.size()));
  }
}

}  // namespace

VerbRecord GetSymSearchVerbRecord() {
  VerbRecord search(&RunVerbSymSearch, {"sym-search"}, kSymSearchShortHelp, kSymSearchHelp,
                    CommandGroup::kSymbol);
  search.switches.emplace_back(kSymSearchListAll, false, "all", 'a');
  search.switches.emplace_back(kSymSearchUnfold, false, "unfold", 'u');
  return search;
}

}  // namespace zxdb
