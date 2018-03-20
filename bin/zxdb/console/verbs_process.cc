// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Checks that the given target can be run or attached and returns Er().
// Otherwise returns an error describing the problem.
Err AssertRunnableTarget(Target* target) {
  Target::State state = target->GetState();
  if (state == Target::State::kStarting) {
    return Err(
        "The current process is in the process of starting.\n"
        "Either \"kill\" it or create a \"new\" process context.");
  }
  if (state == Target::State::kRunning) {
    return Err(
        "The current process is already running.\n"
        "Either \"kill\" it or create a \"new\" process context.");
  }
  return Err();
}

// Callback for both "run" and "attach". The verb affects the message printed
// to the screen.
void RunAndAttachCallback(const char* verb, Target* target, const Err& err) {
  Console* console = Console::get();

  OutputBuffer out;
  if (err.has_error()) {
    out.Append(fxl::StringPrintf("Process %d %s failed.\n",
                                 console->context().IdForTarget(target), verb));
    out.OutputErr(err);
  } else {
    out.Append(DescribeTarget(&console->context(), target, false));
  }

  console->Output(std::move(out));
}

// new -------------------------------------------------------------------------

const char kNewShortHelp[] = "new: Create a new process context.";
const char kNewHelp[] =
    R"(new

  Creates a new process context. A process context holds settings (binary name,
  command line arguments, etc.) and possibly a running process. The new
  context will have no associated process and can then be run or attached.

  The settings from the current process context will be cloned. If an explicit
  process is specified ("process 2 new"), the new process context will clone
  the given one. The new context will be the active context.

Hints

  To see a list of available process contexts, type "process". To switch the
  active process context, specify its index ("process 3").

Example

  This example creates two processes, a "chrome" process, and attaches to
  some existing process.

  [zxdb] run chrome
  Process 1 Running 3456 chrome
  [zxdb] new
  Process 2 created.
  [zxdb] attach 1239
  Process 2 Running 1239
)";
Err DoNew(ConsoleContext* context, const Command& cmd) {
  Target* new_target =
      context->session()->system().CreateNewTarget(cmd.target());
  context->SetActiveTarget(new_target);
  Console::get()->Output(DescribeTarget(context, new_target, false));
  return Err();
}

// run -------------------------------------------------------------------------

const char kRunShortHelp[] = "run / r: Run the program.";
const char kRunHelp[] =
    R"(run [ <program name> ]

  Alias: "r"

  Runs the program. With no arguments, "run" will run the binary stored in the
  process context, if any. With an argument, the binary name will be set and
  that binary will be run.

Hints

  By default "run" will run the active process context (create a new one with
  "new" to run multiple programs at once). To run an explicit process context,
  specify it explicitly: "process 2 run".

  To see a list of available process contexts, type "process".

Examples

  run
  run chrome
  process 2 run
)";

Err DoRun(ConsoleContext* context, const Command& cmd) {
  // Only a process can be run.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  err = AssertRunnableTarget(cmd.target());
  if (err.has_error())
    return err;

  // TODO(brettw) figure out how argument passing should work. From a user
  // perspective it would be nicest to pass everything after "run" to the
  // app. But this means we can't have any switches to "run". LLDB requires
  // using "--" for this case to mark the end of switches.
  if (cmd.args().empty()) {
    // Use the args already set on the target.
    if (cmd.target()->GetArgs().empty())
      return Err("No program to run. Try \"run <program name>\".");
  } else {
    cmd.target()->SetArgs(cmd.args());
  }

  cmd.target()->Launch([](Target* target, const Err& err) {
    RunAndAttachCallback("launch", target, err);
  });
  return Err();
}

// attach ----------------------------------------------------------------------

const char kAttachShortHelp[] = "attach: Attach to a running process.";
const char kAttachHelp[] =
    R"(attach <process koid>

Hints

  Use the "ps" command to view the active process tree.

  To debug more than one process at a time, use "new" to create a new process
  context.

Examples

  attach 2371
      Attaches to the process with koid 2371.

  process 4 attach 2371
      Attaches process context 4 to the process with koid 2371.
)";
Err DoAttach(ConsoleContext* context, const Command& cmd) {
  // Only a process can be attached.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  err = AssertRunnableTarget(cmd.target());
  if (err.has_error())
    return err;

  // Should have one arg which is the koid.
  uint64_t koid = 0;
  err = ReadUint64Arg(cmd, 0, "process koid", &koid);
  if (err.has_error())
    return err;

  cmd.target()->Attach(koid, [](Target* target, const Err& err) {
    RunAndAttachCallback("attach", target, err);
  });
  return Err();
}

// detach ----------------------------------------------------------------------

const char kDetachShortHelp[] = "detach: Detach from a process.";
const char kDetachHelp[] =
    R"(detach

  Detaches the debugger from a running process. The process will continue
  running.

Hints

  By default the current process is detached.
  To detach a different process prefix with "process N"

Examples

  detach
      Detaches from the current process.

  process 4 detach
      Detaches from process context 4.
)";
Err DoDetach(ConsoleContext* context, const Command& cmd) {
  // Only a process can be detached.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  // Only print something when there was an error detaching. The console
  // context will watch for Process destruction and print messages for each one
  // in the success case.
  cmd.target()->Detach([](Target* target, const Err& err) {
    if (err.has_error()) {
      Console* console = Console::get();
      OutputBuffer out;
      out.Append(fxl::StringPrintf("Process %d detach failed.\n",
          console->context().IdForTarget(target)));
      out.OutputErr(err);
      console->Output(std::move(out));
    }
  });
  return Err();
}

}  // namespace

void AppendProcessVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kNew] = VerbRecord(&DoNew, {"new"}, kNewShortHelp, kNewHelp);
  (*verbs)[Verb::kRun] =
      VerbRecord(&DoRun, {"run", "r"}, kRunShortHelp, kRunHelp);
  (*verbs)[Verb::kAttach] =
      VerbRecord(&DoAttach, {"attach"}, kAttachShortHelp, kAttachHelp);
  (*verbs)[Verb::kDetach] =
      VerbRecord(&DoDetach, {"detach"}, kDetachShortHelp, kDetachHelp);
}

}  // namespace zxdb
