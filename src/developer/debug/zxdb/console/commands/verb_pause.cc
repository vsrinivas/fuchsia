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
  if (Err err = VerifySystemHasRunningProcess(system); err.has_error())
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

Err RunVerbPause(ConsoleContext* context, const Command& cmd) {
  if (Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread}); err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kThread))
    return PauseThread(context, cmd.thread());
  if (cmd.HasNoun(Noun::kProcess))
    return PauseTarget(context, cmd.target(), cmd.thread());
  return PauseSystem(&context->session()->system(), cmd.thread());
}

}  // namespace

VerbRecord GetPauseVerbRecord() {
  return VerbRecord(&RunVerbPause, {"pause", "pa"}, kPauseShortHelp, kPauseHelp,
                    CommandGroup::kProcess);
}

}  // namespace zxdb
