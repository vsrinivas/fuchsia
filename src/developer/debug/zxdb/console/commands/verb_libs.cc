// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_libs.h"

#include <algorithm>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kLibsShortHelp[] = "libs: Show loaded libraries for a process.";
const char kLibsHelp[] =
    R"(libs

  Shows the loaded library information for the given process.

Examples

  libs
  process 2 libs
)";

// Completion callback for DoLibs().
void OnLibsComplete(const Err& err, std::vector<debug_ipc::Module> modules) {
  Console* console = Console::get();
  if (err.has_error()) {
    console->Output(err);
    return;
  }

  // Sort by load address.
  std::sort(modules.begin(), modules.end(),
            [](const debug_ipc::Module& a, const debug_ipc::Module& b) { return a.base < b.base; });

  std::vector<std::vector<std::string>> rows;
  for (const auto& module : modules) {
    rows.push_back(
        std::vector<std::string>{fxl::StringPrintf("0x%" PRIx64, module.base), module.name});
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Load address", 2), ColSpec(Align::kLeft, 0, "Name", 1)},
              rows, &out);
  console->Output(out);
}

Err RunVerbLibs(ConsoleContext* context, const Command& cmd) {
  // Only a process can be specified.
  if (Err err = cmd.ValidateNouns({Noun::kProcess}); err.has_error())
    return err;

  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"libs\" takes no parameters.");

  if (Err err = AssertRunningTarget(context, "libs", cmd.target()); err.has_error())
    return err;

  cmd.target()->GetProcess()->GetModules(&OnLibsComplete);
  return Err();
}

}  // namespace

VerbRecord GetLibsVerbRecord() {
  return VerbRecord(&RunVerbLibs, {"libs"}, kLibsShortHelp, kLibsHelp, CommandGroup::kQuery);
}

}  // namespace zxdb
