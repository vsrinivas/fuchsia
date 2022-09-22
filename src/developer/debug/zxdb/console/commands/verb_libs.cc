// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_libs.h"

#include <algorithm>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/string_util.h"
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
void OnLibsComplete(std::vector<debug_ipc::Module> modules,
                    fxl::RefPtr<CommandContext> cmd_context) {
  // Sort by load address.
  std::sort(modules.begin(), modules.end(),
            [](const debug_ipc::Module& a, const debug_ipc::Module& b) { return a.base < b.base; });

  std::vector<std::vector<std::string>> rows;
  for (const auto& module : modules) {
    rows.push_back(std::vector<std::string>{to_hex_string(module.base), module.name});
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Load address", 2), ColSpec(Align::kLeft, 0, "Name", 1)},
              rows, &out);
  cmd_context->Output(out);
}

void RunVerbLibs(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  // Only a process can be specified.
  if (Err err = cmd.ValidateNouns({Noun::kProcess}); err.has_error())
    return cmd_context->ReportError(err);

  if (!cmd.args().empty())
    return cmd_context->ReportError(Err(ErrType::kInput, "\"libs\" takes no parameters."));

  if (Err err = AssertRunningTarget(cmd_context->GetConsoleContext(), "libs", cmd.target());
      err.has_error())
    return cmd_context->ReportError(err);

  cmd.target()->GetProcess()->GetModules([cmd_context](auto err, auto modules) {
    if (err.has_error())
      return cmd_context->ReportError(err);
    OnLibsComplete(modules, cmd_context);
  });
}

}  // namespace

VerbRecord GetLibsVerbRecord() {
  return VerbRecord(&RunVerbLibs, {"libs"}, kLibsShortHelp, kLibsHelp, CommandGroup::kQuery);
}

}  // namespace zxdb
