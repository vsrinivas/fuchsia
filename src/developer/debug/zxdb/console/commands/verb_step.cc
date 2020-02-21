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

constexpr int kStepIntoUnsymbolized = 1;

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

Arguments

  --unsymbolized | -u
      Force stepping into functions with no symbols. Normally "step" will
      skip over library calls or thunks with no symbols. This option allows
      one to step into these unsymbolized calls.

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

Err RunVerbStep(ConsoleContext* context, const Command& cmd) {
  if (Err err = AssertStoppedThreadCommand(context, cmd, true, "step"); err.has_error())
    return err;

  // All controllers do this on completion.
  auto completion = [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  };

  if (cmd.args().empty()) {
    // Step for a single line.
    auto controller = std::make_unique<StepIntoThreadController>(StepMode::kSourceLine);
    controller->set_stop_on_no_symbols(cmd.HasSwitch(kStepIntoUnsymbolized));
    cmd.thread()->ContinueWith(std::move(controller), std::move(completion));
  } else if (cmd.args().size() == 1) {
    // Step into a specific named subroutine. This uses the "step over" controller with a special
    // condition.
    if (cmd.HasSwitch(kStepIntoUnsymbolized)) {
      return Err(
          "The --unsymbolized switch is not compatible with a named "
          "subroutine to step\ninto.");
    }
    auto controller = std::make_unique<StepOverThreadController>(StepMode::kSourceLine);
    controller->set_subframe_should_stop_callback(
        [substr = cmd.args()[0]](const Frame* frame) -> bool {
          const Symbol* symbol = frame->GetLocation().symbol().Get();
          if (!symbol)
            return false;  // Unsymbolized location, continue.
          return symbol->GetFullName().find(substr) != std::string::npos;
        });
    cmd.thread()->ContinueWith(std::move(controller), std::move(completion));
  } else {
    return Err("Too many arguments for 'step'.");
  }

  return Err();
}

}  // namespace

VerbRecord GetStepVerbRecord() {
  SwitchRecord step_force(kStepIntoUnsymbolized, false, "unsymbolized", 'u');
  VerbRecord step(&RunVerbStep, {"step", "s"}, kStepShortHelp, kStepHelp, CommandGroup::kStep,
                  SourceAffinity::kSource);
  step.switches.push_back(step_force);
  return step;
}

}  // namespace zxdb
