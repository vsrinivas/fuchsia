// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_run.h"

#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kRunShortHelp[] = "run / r: Run the program.";
const char kRunHelp[] =
    R"(run [ <binary path> <program args>* ]

  Alias: "r"

  Run the binary available in debug_agent's namespace.

Why "run" is usually wrong

  "run" can only run the binary available in debug_agent's namespace, which
  only include the debug_agent itself and binaries from the bootfs. It's almost
  certain that the program you are interested cannot be launched via "run".

  Instead, consider

    * Use "run-test" to run a test.
    * Use "run-component" to run a component, although it's also usually wrong.
    * Create a filter by "attach <process name>/<component url>/etc." and start
      your program outside of the debugger.

Examples

  run /boot/bin/ps
  run /boot/bin/crasher log_fatal
)";

void RunVerbRun(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  // Only a process can be run.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return cmd_context->ReportError(err);

  // Output warning about this possibly not working.
  OutputBuffer warning(Syntax::kWarning, GetExclamation());
  warning.Append(
      " Run won't work for many processes and components. "
      "See \"help run\".\n");
  cmd_context->Output(warning);

  // May need to create a new target.
  auto err_or_target = GetRunnableTarget(cmd_context->GetConsoleContext(), cmd);
  if (err_or_target.has_error())
    return cmd_context->ReportError(err_or_target.err());
  Target* target = err_or_target.value();

  if (cmd.args().empty()) {
    // Use the args already set on the target.
    if (target->GetArgs().empty())
      return cmd_context->ReportError(Err("No program to run. Try \"run <program name>\"."));
  } else {
    target->SetArgs(cmd.args());
  }

  target->Launch(
      [cmd_context](fxl::WeakPtr<Target> target, const Err& err, uint64_t timestamp) mutable {
        // The ConsoleContext displays messages for new processes, so don't display messages when
        // successfully starting.
        ProcessCommandCallback(std::move(target), false, err, cmd_context);
      });
}

}  // namespace

VerbRecord GetRunVerbRecord() {
  VerbRecord run(&RunVerbRun, {"run", "r"}, kRunShortHelp, kRunHelp, CommandGroup::kProcess);
  return run;
}

}  // namespace zxdb
