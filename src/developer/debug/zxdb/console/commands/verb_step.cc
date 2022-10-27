// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_step.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/step_into_thread_controller.h"
#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kStepShortHelp[] = "step / s: Step one source line, going into subroutines.";
const char kStepHelp[] =
    R"(step [ <function-fragment> ]

  Alias: "s"

  When a thread is stopped, "step" will execute one source line and stop the
  thread again. This will follow execution into subroutines. If the thread is
  running it will issue an error.

  By default, "step" will single-step the current thread. If a thread context
  is given, the specified thread will be stepped. You can't step a process.
  Other threads in the process will be unchanged so will remain running or
  stopped.

  If the thread ends up in a new function, that function's prologue will be
  automatically skipped before the operation complets. An option to control
  whether this happens can be added in the future if desired (bug 45309).

  See also "stepi".

Stepping into specific functions

  If provided, the parameter will specify a specific function call to step
  into.

  The string will be matched against the symbol names of subroutines called
  directly from the current line. Execution will stop if the function name
  contains this fragment, and automatically complete that function call
  otherwise.

Unsymbolized functions

  The step command follows the "skip-unsymbolized" setting when an unsymbolized
  function is encountered. See "get skip-unsymbolized" for more.

Examples

  s
  step
      Step the current thread.

  t 2 s
  thread 2 step
      Steps thread 2 in the current process.

  s Pri
      Steps into a function with the substring "Pri" anywhere in its name. If
      you have a complex line such as:

        Print(GetFoo(), std::string("bar");

      The "s Pri" command will step over the GetFoo() and std::string() calls,
      and leave execution at the beginning of the "Print" subroutine.
)";

void RunVerbStep(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (Err err = AssertStoppedThreadWithFrameCommand(cmd_context->GetConsoleContext(), cmd, "step");
      err.has_error())
    return cmd_context->ReportError(err);

  // All controllers do this on completion.
  auto completion = [cmd_context](const Err& err) {
    if (err.has_error())
      cmd_context->ReportError(err);
  };

  if (cmd.args().empty()) {
    // Step for a single line.
    auto controller = std::make_unique<StepIntoThreadController>(
        StepMode::kSourceLine, &ScheduleAsyncPrintReturnValue,
        fit::defer_callback([cmd_context]() {}));
    cmd.thread()->ContinueWith(std::move(controller), std::move(completion));
  } else if (cmd.args().size() == 1) {
    // Step into a specific named subroutine. This uses the "step over" controller with a special
    // condition.
    auto controller = std::make_unique<StepOverThreadController>(
        StepMode::kSourceLine, FunctionReturnCallback(), fit::defer_callback([cmd_context]() {}));
    controller->set_subframe_should_stop_callback(
        [substr = cmd.args()[0]](const Frame* frame) -> bool {
          const Symbol* symbol = frame->GetLocation().symbol().Get();
          if (!symbol)
            return false;  // Unsymbolized location, continue.
          return symbol->GetFullName().find(substr) != std::string::npos;
        });
    cmd.thread()->ContinueWith(std::move(controller), std::move(completion));
  } else {
    return cmd_context->ReportError(Err("Too many arguments for 'step'."));
  }
}

}  // namespace

VerbRecord GetStepVerbRecord() {
  return VerbRecord(&RunVerbStep, {"step", "s"}, kStepShortHelp, kStepHelp, CommandGroup::kStep,
                    SourceAffinity::kSource);
}

}  // namespace zxdb
