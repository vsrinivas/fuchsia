// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/step_into_specific_thread_controller.h"
#include "src/developer/debug/zxdb/client/step_into_thread_controller.h"
#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/substatement.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/until_thread_controller.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/commands/verb_steps.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_frame.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/print_command_utils.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/developer/debug/zxdb/symbols/visit_scopes.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kStepIntoUnsymbolized = 1;

// If the system has at least one running process, returns true. If not, returns false and sets the
// err.
//
// When doing global things like System::Continue(), it will succeed if there are no running
// programs (it will successfully continue all 0 processes). This is confusing to the user so this
// function is used to check first.
bool VerifySystemHasRunningProcess(System* system, Err* err) {
  for (const Target* target : system->GetTargets()) {
    if (target->GetProcess())
      return true;
  }
  *err = Err("No processes are running.");
  return false;
}

// continue ----------------------------------------------------------------------------------------

const char kContinueShortHelp[] = "continue / c: Continue a suspended thread or process.";
const char kContinueHelp[] =
    R"(continue / c

  When a thread is stopped at an exception or a breakpoint, "continue" will
  continue execution.

  See "pause" to stop a running thread or process.

  The behavior will depend upon the context specified.

  - By itself, "continue" will continue all threads of all processes that are
    currently stopped.

  - When a process is specified ("process 2 continue" for an explicit process
    or "process continue" for the current process), only the threads in that
    process will be continued. Other debugged processes currently stopped will
    remain so.

  - When a thread is specified ("thread 1 continue" for an explicit thread
    or "thread continue" for the current thread), only that thread will be
    continued. Other threads in that process and other processes currently
    stopped will remain so.

  TODO(brettw) it might be nice to have a --other flag that would continue
  all threads other than the specified one (which the user might want to step
  while everything else is going).

Examples

  c
  continue
      Continue all processes and threads.

  pr c
  process continue
  process 4 continue
      Continue all threads of a process (the current process is implicit if
      no process index is specified).

  t c
  thread continue
  pr 2 t 4 c
  process 2 thread 4 continue
      Continue only one thread (the current process and thread are implicit
      if no index is specified).
)";
Err DoContinue(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kThread)) {
    cmd.thread()->Continue();
  } else if (cmd.HasNoun(Noun::kProcess)) {
    Process* process = cmd.target()->GetProcess();
    if (!process)
      return Err("Process not running, can't continue.");
    process->Continue();
  } else {
    if (!VerifySystemHasRunningProcess(&context->session()->system(), &err))
      return err;
    context->session()->system().Continue();
  }

  return Err();
}

// finish ------------------------------------------------------------------------------------------

const char kFinishShortHelp[] = "finish / fi: Finish execution of a stack frame.";
const char kFinishHelp[] =
    R"(finish / fi

  Alias: "fi"

  Resume thread execution until the selected stack frame returns. This means
  that the current function call will execute normally until it finished.

  See also "until".

Examples

  fi
  finish
      Exit the currently selected stack frame (see "frame").

  pr 1 t 4 fi
  process 1 thead 4 finish
      Applies "finish" to process 1, thread 4.

  f 2 fi
  frame 2 finish
      Exit frame 2, leaving program execution in what was frame 3. Try also
      "frame 3 until" which will do the same thing when the function is not
      recursive.
)";
Err DoFinish(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadWithFrameCommand(context, cmd, "finish");
  if (err.has_error())
    return err;

  Stack& stack = cmd.thread()->GetStack();
  size_t frame_index;
  if (auto found_frame_index = stack.IndexForFrame(cmd.frame()))
    frame_index = *found_frame_index;
  else
    return Err("Internal error, frame not found in current thread.");

  auto controller = std::make_unique<FinishThreadController>(stack, frame_index);
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// jump --------------------------------------------------------------------------------------------

const char kJumpShortHelp[] = "jump / jmp: Set the instruction pointer to a different address.";
const char kJumpHelp[] =
    R"(jump <location>

  Alias: "jmp"

  Sets the instruction pointer of the thread to the given address. It does not
  continue execution. You can "step" or "continue" from the new location.

  You are responsible for what this means semantically since one can't
  generally change the instruction flow and expect things to work.

Location arguments

)" LOCATION_ARG_HELP("jump");
Err DoJump(ConsoleContext* context, const Command& cmd) {
  if (Err err = AssertStoppedThreadCommand(context, cmd, true, "jump"); err.has_error())
    return err;

  if (cmd.args().size() != 1)
    return Err("The 'jump' command requires one argument for the location.");

  Location location;
  if (Err err = ResolveUniqueInputLocation(cmd.frame(), cmd.args()[0], true, &location);
      err.has_error())
    return err;

  cmd.thread()->JumpTo(location.address(), [thread = cmd.thread()->GetWeakPtr()](const Err& err) {
    Console* console = Console::get();
    if (err.has_error()) {
      console->Output(err);
    } else if (thread) {
      // Reset the current stack frame to the top to reflect the location the user has just jumped
      // to.
      console->context().SetActiveFrameIdForThread(thread.get(), 0);

      // Tell the user where they are.
      console->context().OutputThreadContext(thread.get(), debug_ipc::ExceptionType::kNone, {});
    }
  });

  return Err();
}

// next --------------------------------------------------------------------------------------------

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
Err DoNext(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "next");
  if (err.has_error())
    return err;

  auto controller = std::make_unique<StepOverThreadController>(StepMode::kSourceLine);
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// nexti -----------------------------------------------------------------------

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
Err DoNexti(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "nexti");
  if (err.has_error())
    return err;

  auto controller = std::make_unique<StepOverThreadController>(StepMode::kInstruction);
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// pause -------------------------------------------------------------------------------------------

const char kPauseShortHelp[] = "pause / pa: Pause a thread or process.";
const char kPauseHelp[] =
    R"(pause / pa

  When a thread or process is running, "pause" will stop execution so state
  can be inspected or the thread single-stepped.

  See "continue" to resume a paused thread or process.

  The behavior will depend upon the context specified.

  - By itself, "pause" will pause all threads of all processes that are
    currently running.

  - When a process is specified ("process 2 pause" for an explicit process
    or "process pause" for the current process), only the threads in that
    process will be paused. Other debugged processes currently running will
    remain so.

  - When a thread is specified ("thread 1 pause" for an explicit thread
    or "thread pause" for the current thread), only that thread will be
    paused. Other threads in that process and other processes currently
    running will remain so.

  TODO(brettw) it might be nice to have a --other flag that would pause
  all threads other than the specified one.

Examples

  pa
  pause
      Pause all processes and threads.

  pr pa
  process pause
  process 4 pause
      Pause all threads of a process (the current process is implicit if
      no process index is specified).

  t pa
  thread pause
  pr 2 t 4 pa
  process 2 thread 4 pause
      Pause only one thread (the current process and thread are implicit
      if no index is specified).
)";

Err PauseThread(ConsoleContext* context, Thread* thread) {
  // Only save the thread (for printing source info) if it's the current thread.
  Target* target = thread->GetProcess()->GetTarget();
  bool show_source =
      context->GetActiveTarget() == target && context->GetActiveThreadForTarget(target) == thread;

  thread->Pause([weak_thread = thread->GetWeakPtr(), show_source]() {
    if (!weak_thread)
      return;

    Console* console = Console::get();
    if (show_source) {
      // Output the full source location.
      console->context().OutputThreadContext(weak_thread.get(), debug_ipc::ExceptionType::kNone,
                                             {});

    } else {
      // Not current, just output the one-line description.
      OutputBuffer out("Paused ");
      out.Append(FormatThread(&console->context(), weak_thread.get()));
      console->Output(out);
    }
  });

  return Err();
}

// Source information on this thread will be printed out on completion. The current thread may be
// null.
Err PauseTarget(ConsoleContext* context, Target* target, Thread* current_thread) {
  Process* process = target->GetProcess();
  if (!process)
    return Err("Process not running, can't pause.");

  // Only save the thread (for printing source info) if it's the current thread.
  fxl::WeakPtr<Thread> weak_thread;
  if (current_thread && context->GetActiveTarget() == target &&
      context->GetActiveThreadForTarget(target) == current_thread)
    weak_thread = current_thread->GetWeakPtr();

  process->Pause([weak_process = process->GetWeakPtr(), weak_thread]() {
    if (!weak_process)
      return;
    Console* console = Console::get();
    OutputBuffer out("Paused");
    out.Append(FormatTarget(&console->context(), weak_process->GetTarget()));
    console->Output(out);

    if (weak_thread) {
      // Thread is current, show current location.
      console->context().OutputThreadContext(weak_thread.get(), debug_ipc::ExceptionType::kNone,
                                             {});
    }
  });
  return Err();
}

// Source information on this thread will be printed out on completion. The current thread may be
// null.
Err PauseSystem(System* system, Thread* current_thread) {
  Err err;
  if (!VerifySystemHasRunningProcess(system, &err))
    return err;

  fxl::WeakPtr<Thread> weak_thread;
  if (current_thread)
    weak_thread = current_thread->GetWeakPtr();

  system->Pause([weak_system = system->GetWeakPtr(), weak_thread]() {
    // Provide messaging about the system pause.
    if (!weak_system)
      return;
    OutputBuffer out;
    Console* console = Console::get();

    // Collect the status of all running processes.
    int paused_process_count = 0;
    for (const Target* target : weak_system->GetTargets()) {
      if (const Process* process = target->GetProcess()) {
        paused_process_count++;
        out.Append(" " + GetBullet() + " ");
        out.Append(FormatTarget(&console->context(), target));
        out.Append("\n");
      }
    }
    // Skip the process list if there's only one and we're showing the thread info below. Otherwise
    // the one thing paused is duplicated twice and this is the most common case.
    if (paused_process_count > 1 || !weak_thread) {
      console->Output("Paused:\n");
      console->Output(out);
      console->Output("\n");
    }

    // Follow with the source context of the current thread if there is one.
    if (weak_thread) {
      console->context().OutputThreadContext(weak_thread.get(), debug_ipc::ExceptionType::kNone,
                                             {});
    }
  });
  return Err();
}

Err DoPause(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kThread))
    return PauseThread(context, cmd.thread());
  if (cmd.HasNoun(Noun::kProcess))
    return PauseTarget(context, cmd.target(), cmd.thread());
  return PauseSystem(&context->session()->system(), cmd.thread());
}

// step --------------------------------------------------------------------------------------------

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
Err DoStep(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "step");
  if (err.has_error())
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

// stepi -------------------------------------------------------------------------------------------

const char kStepiShortHelp[] = "stepi / si: Single-step a thread one machine instruction.";
const char kStepiHelp[] =
    R"(stepi / si

  When a thread is stopped, "stepi" will execute one machine instruction and
  stop the thread again. If the thread is running it will issue an error.

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
Err DoStepi(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "stepi");
  if (err.has_error())
    return err;

  cmd.thread()->StepInstruction();
  return Err();
}

// until -------------------------------------------------------------------------------------------

const char kUntilShortHelp[] = "until / u: Runs a thread until a location is reached.";
const char kUntilHelp[] =
    R"(until <location>

  Alias: "u"

  Continues execution of a thread or a process until a given location is
  reached. You could think of this command as setting an implicit one-shot
  breakpoint at the given location and continuing execution.

  Normally this operation will apply only to the current thread. To apply to
  all threads in a process, use "process until" (see the examples below).

  See also "finish".

Location arguments

  Current frame's address (no input)
    until

)" LOCATION_ARG_HELP("until")
        R"(
Examples

  u
  until
      Runs until the current frame's location is hit again. This can be useful
      if the current code is called in a loop to advance to the next iteration
      of the current code.

  f 1 u
  frame 1 until
      Runs until the given frame's location is hit. Since frame 1 is
      always the current function's calling frame, this command will normally
      stop when the current function returns. The exception is if the code
      in the calling function is called recursively from the current location,
      in which case the next invocation will stop ("until" does not match
      stack frames on break). See "finish" for a stack-aware version.

  u 24
  until 24
      Runs the current thread until line 24 of the current frame's file.

  until foo.cc:24
      Runs the current thread until the given file/line is reached.

  thread 2 until 24
  process 1 thread 2 until 24
      Runs the specified thread until line 24 is reached. When no filename is
      given, the specified thread's currently selected frame will be used.

  u MyClass::MyFunc
  until MyClass::MyFunc
      Runs the current thread until the given function is called.

  pr u MyClass::MyFunc
  process until MyClass::MyFunc
      Continues all threads of the current process, stopping the next time any
      of them call the function.
)";
Err DoUntil(ConsoleContext* context, const Command& cmd) {
  Err err;

  // Decode the location.
  //
  // The validation on this is a bit tricky. Most uses apply to the current thread and take some
  // implicit information from the current frame (which requires the thread be stopped). But when
  // doing a process-wide one, don't require a currently stopped thread unless it's required to
  // compute the location.
  std::vector<InputLocation> locations;
  if (cmd.args().empty()) {
    // No args means use the current location.
    if (!cmd.frame()) {
      return Err(ErrType::kInput, "There isn't a current frame to take the location from.");
    }
    locations.emplace_back(cmd.frame()->GetAddress());
  } else if (cmd.args().size() == 1) {
    // One arg = normal location (this function can handle null frames).
    Err err = ParseLocalInputLocation(cmd.frame(), cmd.args()[0], &locations);
    if (err.has_error())
      return err;
  } else {
    return Err(ErrType::kInput,
               "Expecting zero or one arg for the location.\n"
               "Formats: <function>, <file>:<line#>, <line#>, or *<address>");
  }

  auto callback = [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  };

  // Dispatch the request.
  if (cmd.HasNoun(Noun::kProcess) && !cmd.HasNoun(Noun::kThread) && !cmd.HasNoun(Noun::kFrame)) {
    // Process-wide ("process until ...").
    err = AssertRunningTarget(context, "until", cmd.target());
    if (err.has_error())
      return err;
    cmd.target()->GetProcess()->ContinueUntil(locations, callback);
  } else {
    // Thread-specific.
    err = AssertStoppedThreadWithFrameCommand(context, cmd, "until");
    if (err.has_error())
      return err;

    auto controller = std::make_unique<UntilThreadController>(std::move(locations));
    cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
      if (err.has_error())
        Console::get()->Output(err);
    });
  }
  return Err();
}

}  // namespace

void AppendThreadVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kContinue] =
      VerbRecord(&DoContinue, {"continue", "cont", "c"}, kContinueShortHelp, kContinueHelp,
                 CommandGroup::kStep, SourceAffinity::kSource);
  (*verbs)[Verb::kFinish] =
      VerbRecord(&DoFinish, {"finish", "fi"}, kFinishShortHelp, kFinishHelp, CommandGroup::kStep);
  (*verbs)[Verb::kJump] = VerbRecord(&DoJump, &CompleteInputLocation, {"jump", "jmp"},
                                     kJumpShortHelp, kJumpHelp, CommandGroup::kStep);

  (*verbs)[Verb::kNext] = VerbRecord(&DoNext, {"next", "n"}, kNextShortHelp, kNextHelp,
                                     CommandGroup::kStep, SourceAffinity::kSource);
  (*verbs)[Verb::kNexti] = VerbRecord(&DoNexti, {"nexti", "ni"}, kNextiShortHelp, kNextiHelp,
                                      CommandGroup::kAssembly, SourceAffinity::kAssembly);
  (*verbs)[Verb::kPause] =
      VerbRecord(&DoPause, {"pause", "pa"}, kPauseShortHelp, kPauseHelp, CommandGroup::kProcess);

  // step
  SwitchRecord step_force(kStepIntoUnsymbolized, false, "unsymbolized", 'u');
  VerbRecord step(&DoStep, {"step", "s"}, kStepShortHelp, kStepHelp, CommandGroup::kStep,
                  SourceAffinity::kSource);
  step.switches.push_back(step_force);
  (*verbs)[Verb::kStep] = std::move(step);

  (*verbs)[Verb::kStepi] = VerbRecord(&DoStepi, {"stepi", "si"}, kStepiShortHelp, kStepiHelp,
                                      CommandGroup::kAssembly, SourceAffinity::kAssembly);
  (*verbs)[Verb::kSteps] = GetStepsVerbRecord();
  (*verbs)[Verb::kUntil] = VerbRecord(&DoUntil, &CompleteInputLocation, {"until", "u"},
                                      kUntilShortHelp, kUntilHelp, CommandGroup::kStep);
}

}  // namespace zxdb
