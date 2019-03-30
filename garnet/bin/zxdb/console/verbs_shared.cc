// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/client/session.h"

namespace zxdb {

namespace {

// new -------------------------------------------------------------------------

const char kNewShortHelp[] = "new: Create a new process/job context.";
const char kNewHelp[] =
    R"(new

  Creates a new process/job context.

  A process context holds settings (binary name, command line arguments, etc.)
  and possibly a running process. The new context will have no associated
  process and can then be run or attached.

  A job context holds settings (filters, etc.)
  and possibly a running job. The new context will have no associated
  job and can then be run or attached.

  The settings from the current process/job context will be cloned. If an explicit
  process/job is specified ("process 2 new"), the new process/job context will clone
  the given one. The new context will be the active context.

  A process/job noun must be specified. Long-term we want to add support to "new"
  multiple things.

Hints

  To see a list of available process/job contexts, type "process" or "job". To switch the
  active process context, specify its index ("(process|job) 3").

Example

  This example creates two processes, a "chrome" process, and attaches to
  some existing process.

  [zxdb] run chrome
  Process 1 Running 3456 chrome
  [zxdb] process new
  Process 2 created.
  [zxdb] pr attach 1239
  Process 2 Running 1239

  This example attaches to some existing job.
  [zxdb] job new
  Job 2 created.
  [zxdb] j attach 1239
  Job 2 Running 1239
)";
Err DoNew(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kJob});
  if (err.has_error())
    return err;

  if (!cmd.HasNoun(Noun::kProcess) && !cmd.HasNoun(Noun::kJob))
    return Err("Use \"process new\" or \"job new\" to create a new context.");

  if (cmd.HasNoun(Noun::kJob)) {
    JobContext* new_job_context =
        context->session()->system().CreateNewJobContext(cmd.job_context());
    context->SetActiveJobContext(new_job_context);
    Console::get()->Output(DescribeJobContext(context, new_job_context));
  } else {
    Target* new_target =
        context->session()->system().CreateNewTarget(cmd.target());
    context->SetActiveTarget(new_target);
    Console::get()->Output(DescribeTarget(context, new_target));
  }
  return Err();
}

}  // namespace

void AppendSharedVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kNew] = VerbRecord(&DoNew, {"new"}, kNewShortHelp, kNewHelp,
                                    CommandGroup::kGeneral);
}

}  // namespace zxdb
