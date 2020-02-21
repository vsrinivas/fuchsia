// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_continue.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

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

Err RunVerbContinue(ConsoleContext* context, const Command& cmd) {
  if (Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread}); err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kThread)) {
    cmd.thread()->Continue();
  } else if (cmd.HasNoun(Noun::kProcess)) {
    Process* process = cmd.target()->GetProcess();
    if (!process)
      return Err("Process not running, can't continue.");
    process->Continue();
  } else {
    if (Err err = VerifySystemHasRunningProcess(&context->session()->system()); err.has_error())
      return err;
    context->session()->system().Continue();
  }

  return Err();
}

}  // namespace

VerbRecord GetContinueVerbRecord() {
  return VerbRecord(&RunVerbContinue, {"continue", "cont", "c"}, kContinueShortHelp, kContinueHelp,
                    CommandGroup::kStep, SourceAffinity::kSource);
}

}  // namespace zxdb
