// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_pause.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

constexpr int kClearSwitch = 1;

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

Options

  --clear-state | -c
      Additionally clears all stepping state. Without this flag, any previous
      step operations that have not completed will be resumed when the thread
      is continued.

      Examples of stepping state are the "finish" or "until" commands that may
      take some time to complete. If you run "pause" without "-c" and then
      "continue", the uncompleted "finish" or "until" commands will still be
      active and will automatically stop execution when their condition has been
      fulfilled. The "-c" option will cancel these pending step operations.

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

void PauseThread(fxl::RefPtr<CommandContext> cmd_context, Thread* thread, bool clear_state) {
  // Guaranteed non-null since we're being called synchronously.
  ConsoleContext* console_context = cmd_context->GetConsoleContext();

  // Only save the thread (for printing source info) if it's the current thread.
  Target* target = thread->GetProcess()->GetTarget();
  bool show_source = console_context->GetActiveTarget() == target &&
                     console_context->GetActiveThreadForTarget(target) == thread;

  if (clear_state)
    thread->CancelAllThreadControllers();
  thread->Pause([weak_thread = thread->GetWeakPtr(), show_source, cmd_context]() {
    ConsoleContext* console_context = cmd_context->GetConsoleContext();
    if (!weak_thread || !console_context)
      return;

    if (show_source) {
      // Output the full source location.
      cmd_context->Output(console_context->GetThreadContext(weak_thread.get(), StopInfo()));
    } else {
      // Not current, just output the one-line description.
      OutputBuffer out("Paused ");
      out.Append(FormatThread(console_context, weak_thread.get()));
      cmd_context->Output(out);
    }
  });
}

// Source information on this thread will be printed out on completion. The current thread may be
// null.
void PauseTarget(fxl::RefPtr<CommandContext> cmd_context, Target* target, Thread* current_thread,
                 bool clear_state) {
  // Guaranteed non-null since we're being called synchronously.
  ConsoleContext* console_context = cmd_context->GetConsoleContext();

  Process* process = target->GetProcess();
  if (!process)
    return cmd_context->ReportError(Err("Process not running, can't pause."));

  // Only save the thread (for printing source info) if it's the current thread.
  fxl::WeakPtr<Thread> weak_thread;
  if (current_thread && console_context->GetActiveTarget() == target &&
      console_context->GetActiveThreadForTarget(target) == current_thread)
    weak_thread = current_thread->GetWeakPtr();

  if (clear_state)
    process->CancelAllThreadControllers();
  process->Pause([weak_process = process->GetWeakPtr(), weak_thread, cmd_context]() {
    ConsoleContext* console_context = cmd_context->GetConsoleContext();
    if (!weak_process || !console_context)
      return;

    OutputBuffer out("Paused");
    out.Append(FormatTarget(console_context, weak_process->GetTarget()));
    cmd_context->Output(out);

    if (weak_thread) {
      // Thread is current, show current location.
      cmd_context->Output(console_context->GetThreadContext(weak_thread.get(), StopInfo()));
    }
  });
}

// Source information on this thread will be printed out on completion.
void PauseSystem(fxl::RefPtr<CommandContext> cmd_context, System* system, bool clear_state) {
  if (Err err = VerifySystemHasRunningProcess(system); err.has_error())
    return cmd_context->ReportError(err);

  if (clear_state)
    system->CancelAllThreadControllers();
  system->Pause([weak_system = system->GetWeakPtr(), cmd_context]() {
    // Provide messaging about the system pause.
    ConsoleContext* console_context = cmd_context->GetConsoleContext();
    if (!weak_system || !console_context)
      return;
    OutputBuffer out;

    // Find the current thread for outputting context. The current thread may have changed from
    // when the command was initiated so always use the current one. In addition, pausing a program
    // immediately after starting or attaching to it won't always sync the threads so there might
    // be no thread context on the original "pause" command.
    Thread* thread = nullptr;
    if (const Target* target = console_context->GetActiveTarget())
      thread = console_context->GetActiveThreadForTarget(target);  // May be null if not running.

    // Collect the status of all running processes.
    int paused_process_count = 0;
    for (const Target* target : weak_system->GetTargets()) {
      if (const Process* process = target->GetProcess()) {
        paused_process_count++;
        out.Append(" " + GetBullet() + " ");
        out.Append(FormatTarget(console_context, target));
        out.Append("\n");
      }
    }
    // Skip the process list if there's only one and we're showing the thread info below. Otherwise
    // the one thing paused is duplicated twice and this is the most common case.
    if (paused_process_count > 1 || !thread) {
      cmd_context->Output("Paused:\n");
      cmd_context->Output(out);
      cmd_context->Output("\n");
    }

    // Follow with the source context of the current thread if there is one.
    if (thread)
      cmd_context->Output(console_context->GetThreadContext(thread, StopInfo()));
  });
}

void RunVerbPause(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (Err err = cmd.ValidateNouns({Noun::kGlobal, Noun::kProcess, Noun::kThread}); err.has_error())
    return cmd_context->ReportError(err);

  bool clear_state = cmd.HasSwitch(kClearSwitch);

  if (cmd.HasNoun(Noun::kThread)) {
    PauseThread(cmd_context, cmd.thread(), clear_state);
  } else if (cmd.HasNoun(Noun::kProcess)) {
    PauseTarget(cmd_context, cmd.target(), cmd.thread(), clear_state);
  } else {
    PauseSystem(cmd_context, &cmd_context->GetConsoleContext()->session()->system(), clear_state);
  }
}

}  // namespace

VerbRecord GetPauseVerbRecord() {
  VerbRecord pause(&RunVerbPause, {"pause", "pa"}, kPauseShortHelp, kPauseHelp,
                   CommandGroup::kProcess);
  pause.switches.push_back(SwitchRecord(kClearSwitch, false, "clear-state", 'c'));
  return pause;
}

}  // namespace zxdb
