// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_attach.h"

#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/job_context.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kAttachShortHelp[] = "attach: Attach to a running process/job.";
const char kAttachHelp[] =
    R"(attach <what>

  Attaches to a current or future process.

Atttaching to a specific process

  To attach to a specific process, supply the process' koid (process ID).
  For example:

    attach 12345

  Use the "ps" command to view the active processes, their names, and koids.

Attaching to processes by name

  Non-numeric arguments will be interpreted as a filter. A filter is a substring
  that matches any part of the process name. The filter "t" will match any
  process with the letter "t" in its name. Filters are not regular expressions.

  Filters are applied to processes launched in jobs the debugger is attached to,
  both current processes and future ones.

  More on jobs:

    • See the currently attached jobs with the "job" command.

    • Attach to a new job with the "attach-job" command.

  More on filters:

    • See the current filters with the "filter" command.

    • Delete a filter with "filter [X] rm" where X is the filter index from the
      "filter" list. If no filter index is provided, the current filter will be
      deleted.

    • Change a filter's pattern with "filter [X] set pattern = <newvalue>".

    • Attach to all processes in a job with "job attach *". Note that * is a
      special string for filters, regular expressions are not supported.

  If a job prefix is specified, only processes launched in that job matching the
  pattern will be attached to:

    job attach foo      // Uses the current job context.
    job 2 attach foo    // Specifies job context #2.

  If you have a specific job koid (12345) and want to watch "foo" processes in
  it, a faster way is:

    attach-job 12345 foo

Examples

  attach 2371
      Attaches to the process with koid 2371.

  process 4 attach 2371
      Attaches process context 4 to the process with koid 2371.

  attach foobar
      Attaches to any process that spawns under any job the debugger is attached
      to with "foobar" in the name.

  job 3 attach foobar
      Attaches to any process that spawns under job 3 with "foobar" in the
      name.
)";

Err RunVerbAttach(ConsoleContext* context, const Command& cmd, CommandCallback callback) {
  // Only a process can be attached.
  if (Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kJob}); err.has_error())
    return err;

  uint64_t koid = 0;
  if (ReadUint64Arg(cmd, 0, "process koid", &koid).ok()) {
    // Attach to a process by KOID.
    auto err_or_target = GetRunnableTarget(context, cmd);
    if (err_or_target.has_error())
      return err_or_target.err();
    err_or_target.value()->Attach(koid, [callback = std::move(callback)](
                                            fxl::WeakPtr<Target> target, const Err& err) mutable {
      ProcessCommandCallback(target, true, err, std::move(callback));
    });
    return Err();
  }

  // Not a number, make a filter instead. This only supports only "job" nouns.
  if (cmd.ValidateNouns({Noun::kJob}).has_error()) {
    return Err(
        "Attaching by process name (a non-numeric argument)\nonly supports the \"job\" noun.");
  }
  if (cmd.args().size() != 1)
    return Err("Wrong number of arguments to attach.");

  JobContext* job = cmd.HasNoun(Noun::kJob) && cmd.job_context() ? cmd.job_context() : nullptr;
  const std::string& pattern = cmd.args()[0];
  if (!job && pattern == Filter::kAllProcessesPattern) {
    // Bad things happen if we try to attach to all processes in the system, try to make this
    // more difficult by preventing attaching to * with no specific job.
    return Err("Use a specific job (\"job 3 attach *\") when attaching to all processes.");
  }

  Filter* filter = context->session()->system().CreateNewFilter();
  filter->SetJob(job);
  filter->SetPattern(cmd.args()[0]);

  context->SetActiveFilter(filter);

  // This doesn't use the default filter formatting to try to make it friendlier for people
  // that are less familiar with the debugger and might be unsure what's happening (this is normally
  // one of the first things people do in the debugger. The filter number is usually not relevant
  // anyway.
  Console::get()->Output("Waiting for process matching \"" + cmd.args()[0] +
                         "\".\n"
                         "Type \"filter\" to see the current filters.");
  if (callback) {
    callback(Err());
  }
  return Err();
}

}  // namespace

VerbRecord GetAttachVerbRecord() {
  VerbRecord attach(&RunVerbAttach, {"attach"}, kAttachShortHelp, kAttachHelp,
                    CommandGroup::kProcess);
  return attach;
}

}  // namespace zxdb
