// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_next.h"

#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kNextShortHelp[] = "next / n: Single-step over one source line.";
const char kNextHelp[] =
    R"(next / n

  When a thread is stopped, "next" will execute one source line, stepping over
  subroutine call instructions, and stop the thread again. If the thread is
  running it will issue an error.

  By default, "next" will operate on the current thread. If a thread context
  is given, the specified thread will be single-stepped. You can't single-step
  a process.

  See also "step" to step into subroutine calls or "nexti" to step machine
  instructions.

Examples

  n
  next
      Step the current thread.

  t 2 n
  thread 2 next
      Steps thread 2 in the current process.

  pr 3 n
  process 3 next
      Steps the current thread in process 3 (regardless of which process is
      the current process).

  pr 3 t 2 n
  process 3 thread 2 next
      Steps thread 2 in process 3.
)";
void RunVerbNext(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (Err err = AssertStoppedThreadWithFrameCommand(cmd_context->GetConsoleContext(), cmd, "next");
      err.has_error())
    return cmd_context->ReportError(err);

  auto controller = std::make_unique<StepOverThreadController>(StepMode::kSourceLine,
                                                               &ScheduleAsyncPrintReturnValue);
  cmd.thread()->ContinueWith(std::move(controller), [cmd_context](const Err& err) {
    if (err.has_error())
      cmd_context->ReportError(err);
  });
}

}  // namespace

VerbRecord GetNextVerbRecord() {
  return VerbRecord(&RunVerbNext, {"next", "n"}, kNextShortHelp, kNextHelp, CommandGroup::kStep,
                    SourceAffinity::kSource);
}

}  // namespace zxdb
