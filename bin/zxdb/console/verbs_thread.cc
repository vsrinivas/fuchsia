// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/input_location_parser.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// If the system has at least one running process, returns true. If not,
// returns false and sets the err.
//
// When doing global things like System::Continue(), it will succeed if there
// are no running prograns (it will successfully continue all 0 processes).
// This is confusing to the user so this function is used to check first.
bool VerifySystemHasRunningProcess(System* system, Err* err) {
  for (const Target* target : system->GetTargets()) {
    if (target->GetProcess())
      return true;
  }
  *err = Err("No processes are running.");
  return false;
}

// continue --------------------------------------------------------------------

const char kContinueShortHelp[] =
    "continue / c: Continue a suspended thread or process.";
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
      Contiue all threads of a process (the current process is implicit if
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

// finish ----------------------------------------------------------------------

const char kFinishShortHelp[] =
    "finish / fi: Finish execution of a stack frame.";
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
  // This command allows "frame" which AssertStoppedThreadCommand doesn't,
  // so pass "false" to disabled noun checking and manually check ourselves.
  Err err = AssertStoppedThreadCommand(context, cmd, false, "finish");
  if (err.has_error())
    return err;
  err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return err;

  cmd.thread()->Finish(cmd.frame(), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// pause -----------------------------------------------------------------------

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
Err DoPause(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kThread)) {
    cmd.thread()->Pause();
  } else if (cmd.HasNoun(Noun::kProcess)) {
    Process* process = cmd.target()->GetProcess();
    if (!process)
      return Err("Process not running, can't pause.");
    process->Pause();
  } else {
    if (!VerifySystemHasRunningProcess(&context->session()->system(), &err))
      return err;
    context->session()->system().Pause();
  }

  return Err();
}

// step ------------------------------------------------------------------------

const char kStepShortHelp[] =
    "step / s: Step one source line, going into subroutines.";
const char kStepHelp[] =
    R"(step

  Alias: "s"

  When a thread is stopped, "step" will execute one source line and stop the
  thread again. This will follow execution into subroutines. If the thread is
  running it will issue an error.

  By default, "step" will single-step the current thread. If a thread context
  is given, the specified thread will be stepped. You can't step a process.
  Other threads in the process will be unchanged so will remain running or
  stopped.

  See also "stepi".

Examples

  s
  step
      Step the current thread.

  t 2 s
  thread 2 step
      Steps thread 2 in the current process.
)";
Err DoStep(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "stepi");
  if (err.has_error())
    return err;

  return cmd.thread()->Step();
}

// stepi -----------------------------------------------------------------------

const char kStepiShortHelp[] =
    "stepi / si: Single-step a thread one machine instruction.";
const char kStepiHelp[] =
    R"(stepi / si

  When a thread is stopped, "stepi" will execute one machine instruction and
  stop the thread again. If the thread is running it will issue an error.

  By default, "stepi" will single-step the current thread. If a thread context
  is given, the specified thread will be single-stepped. You can't single-step
  a process.

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

// regs ------------------------------------------------------------------------

const char kRegsShortHelp[] =
    "regs / rg: Show the current registers for a thread.";
const char kRegsHelp[] =
    R"(regs

  Shows the current registers for  thread.
  Alias: "rg"

Examples

  regs
  thread 4 regs
  process 2 thread 1 regs
)";

void OnRegsComplete(const Err& err,
                    std::vector<debug_ipc::Register> registers) {
  Console* console = Console::get();
  if (err.has_error()) {
    console->Output(err);
    return;
  }

  OutputBuffer out = OutputBuffer::WithContents("REGISTERS:\n");

  out.Append("General Registers:\n");
  out.Append("-------------------------------------------------\n");
  for (auto&& reg : registers) {
    out.Append(fxl::StringPrintf("%4s: 0x%016" PRIx64 "\n", reg.name.c_str(),
                                 reg.value));
  }
  console->Output(std::move(out));
}

Err DoRegs(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "regs");
  if (err.has_error())
    return err;

  cmd.thread()->GetRegisters(&OnRegsComplete);
  return Err();
}

// until -----------------------------------------------------------------------

const char kUntilShortHelp[] =
    "until / u: Runs a thread until a location is reached.";
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
  // The validation on this is a bit tricky. Most uses apply to the current
  // thread and take some implicit information from the current frame (which
  // requires the thread be stopped). But when doing a process-wide one, don't
  // require a currently stopped thread unless it's required to compute the
  // location.
  InputLocation location;
  if (cmd.args().empty()) {
    // No args means use the current location.
    if (!cmd.frame()) {
      return Err(ErrType::kInput,
                 "There isn't a current frame to take the location from.");
    }
    location = InputLocation(cmd.frame()->GetAddress());
  } else if (cmd.args().size() == 1) {
    // One arg = normal location (ParseInputLocation can handle null frames).
    Err err = ParseInputLocation(cmd.frame(), cmd.args()[0], &location);
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
  if (cmd.HasNoun(Noun::kProcess) && !cmd.HasNoun(Noun::kThread) &&
      !cmd.HasNoun(Noun::kFrame)) {
    // Process-wide ("process until ...").
    err = AssertRunningTarget(context, "until", cmd.target());
    if (err.has_error())
      return err;
    cmd.target()->GetProcess()->ContinueUntil(location, callback);
  } else {
    // Thread-specific.
    err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
    if (err.has_error())
      return err;

    err = AssertStoppedThreadCommand(context, cmd, false, "until");
    if (err.has_error())
      return err;
    cmd.thread()->ContinueUntil(location, callback);
  }
  return Err();
}

}  // namespace

void AppendThreadVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kContinue] =
      VerbRecord(&DoContinue, {"continue", "c"}, kContinueShortHelp,
                 kContinueHelp, SourceAffinity::kSource);
  (*verbs)[Verb::kFinish] =
      VerbRecord(&DoFinish, {"finish", "fi"}, kFinishShortHelp, kFinishHelp);
  (*verbs)[Verb::kPause] =
      VerbRecord(&DoPause, {"pause", "pa"}, kPauseShortHelp, kPauseHelp);
  (*verbs)[Verb::kRegs] =
      VerbRecord(&DoRegs, {"regs", "rg"}, kRegsShortHelp, kRegsHelp);
  (*verbs)[Verb::kStep] = VerbRecord(&DoStep, {"step", "s"}, kStepShortHelp,
                                     kStepHelp, SourceAffinity::kSource);
  (*verbs)[Verb::kStepi] =
      VerbRecord(&DoStepi, {"stepi", "si"}, kStepiShortHelp, kStepiHelp,
                 SourceAffinity::kAssembly);
  (*verbs)[Verb::kUntil] =
      VerbRecord(&DoUntil, {"until", "u"}, kUntilShortHelp, kUntilHelp);
}

}  // namespace zxdb
