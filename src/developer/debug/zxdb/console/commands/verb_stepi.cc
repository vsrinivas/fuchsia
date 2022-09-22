// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_stepi.h"

#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kStepiShortHelp[] = "stepi / si: Single-step a thread one machine instruction.";
const char kStepiHelp[] =
    R"(stepi / si [ <count> ]

  When a thread is stopped, "stepi" will execute <count> machine instructions
  and stop the thread again. If <count> is not specified it will default to 1.
  If the thread is running it will issue an error.

  By default, "stepi" will single-step the current thread. If a thread context
  is given, the specified thread will be single-stepped. You can't single-step
  a process.

  See also "nexti" to step over subroutine calls.

Examples

  si
  stepi
      Step the current thread.

  t 2 si
  thread 2 stepi
      Steps thread 2 in the current process.

  pr 3 si
  process 3 stepi
      Steps the current thread in process 3 (regardless of which process is
      the current process).

  pr 3 t 2 si
  process 3 thread 2 stepi
      Steps thread 2 in process 3.
)";

void RunVerbStepi(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (Err err = AssertStoppedThreadWithFrameCommand(cmd_context->GetConsoleContext(), cmd, "stepi");
      err.has_error())
    return cmd_context->ReportError(err);

  uint64_t count = 1;
  if (cmd.args().size() == 1) {
    if (Err err = StringToUint64(cmd.args()[0], &count); err.has_error()) {
      return cmd_context->ReportError(err);
    }
    if (count == 0) {
      return cmd_context->ReportError(Err("<count> must be non-zero."));
    }
  } else if (cmd.args().size() > 1) {
    return cmd_context->ReportError(Err("Too many arguments for 'stepi'."));
  }

  cmd.thread()->StepInstructions(count);
}

}  // namespace

VerbRecord GetStepiVerbRecord() {
  return VerbRecord(&RunVerbStepi, {"stepi", "si"}, kStepiShortHelp, kStepiHelp,
                    CommandGroup::kAssembly, SourceAffinity::kAssembly);
}

}  // namespace zxdb
