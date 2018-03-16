// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// continue --------------------------------------------------------------------

const char kContinueShortHelp[] = "continue / c: Continue a suspended thread.";
const char kContinueHelp[] =
    R"(continue

  Alias: "c"

  When a thread is stopped at an exception or a breakpoint, "continue" will
  continue execution.

  The behavior will depend upon the context specified.

  - By itself, "continue" will continue all threads of all processes that
    are currently stopped.

  - When a process is specified ("process 2 continue" for an explicit process
    or "process continue" for the current process), only the threads in that
    process will be continued. Other debugged processes currently stopped will
    remain so.

  - When a thread is specified ("thread 1 continue" for an explicit thread
    or "thread continue" for the current thread), only that thread will be
    continued. Other threads in that process and other processes currently
    stopped will remain so.

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
  Err err = cmd.ValidateNouns({ Noun::kProcess, Noun::kThread });
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
    // Nothing explicitly specified, continue all processes. If there is no
    // debugged programs, System.Continue() will still work (it will
    // successfully continue 0 processes) which is confusing to users who may
    // not realize the process isn't running.
    bool has_running_target = false;
    for (const Target* target : context->session()->system().GetAllTargets()) {
      if (target->GetProcess()) {
        has_running_target = true;
        break;
      }
    }
    if (!has_running_target)
      return Err("No processes are running to continue.");
    context->session()->system().Continue();
  }

  return Err();
}

}  // namespace

void AppendThreadVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kContinue] =
      VerbRecord(&DoContinue, {"continue", "c"}, kContinueShortHelp,
                 kContinueHelp);
}

}  // namespace zxdb
