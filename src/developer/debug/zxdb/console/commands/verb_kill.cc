// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_kill.h"

#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kKillShortHelp[] = "kill / k: Terminate a process";
const char kKillHelp[] =
    R"(kill

  Terminates a process attached in the debugger.

  By default the current process is detached.

  To detach a different process prefix with "process <number>". To list
  attached processes type "process".

Examples

  kill
      Kills the current process.

  process 4 kill
      Kills process 4.
)";

void RunVerbKill(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  // Only a process can be detached.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return cmd_context->ReportError(err);

  if (!cmd.args().empty())
    return cmd_context->ReportError(Err("The 'kill' command doesn't take any parameters."));

  cmd.target()->Kill([cmd_context](fxl::WeakPtr<Target> target, const Err& err) mutable {
    // The ConsoleContext displays messages for stopped processes, so don't display messages
    // when successfully killing.
    ProcessCommandCallback(target, false, err, cmd_context);
  });
}

}  // namespace

VerbRecord GetKillVerbRecord() {
  return VerbRecord(&RunVerbKill, {"kill", "k"}, kKillShortHelp, kKillHelp, CommandGroup::kProcess);
}

}  // namespace zxdb
