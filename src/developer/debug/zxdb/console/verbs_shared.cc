// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_job.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/developer/debug/zxdb/console/verbs.h"

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
    JobContext* new_job_context = context->session()->system().CreateNewJobContext();
    context->SetActiveJobContext(new_job_context);
    Console::get()->Output(FormatJobContext(context, new_job_context));
  } else {
    Target* new_target = context->session()->system().CreateNewTarget(cmd.target());
    context->SetActiveTarget(new_target);
    Console::get()->Output(FormatTarget(context, new_target));
  }
  return Err();
}

// rm --------------------------------------------------------------------------

const char kRmShortHelp[] = "rm: Remove a filter.";
const char kRmHelp[] =
    R"(rm

  Removes a filter. You must specify the filter explicitly to delete it.

  Filters can be added with the attach command.

Hints

  To see a list of available filters, type "filter".

Example

  filter 3 rm
      Remove filter number 3.
)";
Err DoRm(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kFilter});
  if (err.has_error())
    return err;

  if (!cmd.HasNoun(Noun::kFilter) || cmd.GetNounIndex(Noun::kFilter) == Command::kNoIndex)
    return Err("You must explicitly specify \"filter n rm\" to remove a filter.");

  context->session()->system().DeleteFilter(cmd.filter());

  return Err();
}

}  // namespace

void AppendSharedVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kNew] =
      VerbRecord(&DoNew, {"new"}, kNewShortHelp, kNewHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kRm] = VerbRecord(&DoRm, {"rm"}, kRmShortHelp, kRmHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
