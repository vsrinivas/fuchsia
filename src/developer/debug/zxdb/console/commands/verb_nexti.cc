// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_nexti.h"

#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kNextiShortHelp[] = "nexti / ni: Single-step over one machine instruction.";
const char kNextiHelp[] =
    R"(nexti / ni

  When a thread is stopped, "nexti" will execute one machine instruction,
  stepping over subroutine call instructions, and stop the thread again.
  If the thread is running it will issue an error.

  Only machine call instructions ("call" on x86 and "bl" on ARM) will be
  stepped over with this command. This is not the only way to do a subroutine
  call, as code can manually set up a call frame and jump. These jumps will not
  count as a call and this command will step into the resulting frame.

  By default, "nexti" will operate on the current thread. If a thread context
  is given, the specified thread will be single-stepped. You can't single-step
  a process.

  See also "stepi" to step into subroutine calls.

Examples

  ni
  nexti
      Step the current thread.

  t 2 ni
  thread 2 nexti
      Steps thread 2 in the current process.

  pr 3 ni
  process 3 nexti
      Steps the current thread in process 3 (regardless of which process is
      the current process).

  pr 3 t 2 ni
  process 3 thread 2 nexti
      Steps thread 2 in process 3.
)";

void RunVerbNexti(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  Err err = AssertStoppedThreadWithFrameCommand(cmd_context->GetConsoleContext(), cmd, "nexti");
  if (err.has_error())
    return cmd_context->ReportError(err);

  auto controller = std::make_unique<StepOverThreadController>(StepMode::kInstruction);
  cmd.thread()->ContinueWith(std::move(controller), [cmd_context](const Err& err) {
    if (err.has_error())
      cmd_context->ReportError(err);
  });
}

}  // namespace

VerbRecord GetNextiVerbRecord() {
  return VerbRecord(&RunVerbNexti, {"nexti", "ni"}, kNextiShortHelp, kNextiHelp,
                    CommandGroup::kAssembly, SourceAffinity::kAssembly);
}

}  // namespace zxdb
