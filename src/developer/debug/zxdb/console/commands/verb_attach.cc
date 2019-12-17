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

constexpr int kAttachComponentRootSwitch = 1;
constexpr int kAttachSystemRootSwitch = 2;

const char kAttachShortHelp[] = "attach: Attach to a running process/job.";
const char kAttachHelp[] =
    R"(attach <pattern>

  Attaches to an existing process or job. When no noun is provided it will
  assume the KOID refers to a process. To be explicit, prefix with a "process"
  or "job" noun.

  If the argument is not a number, it will be interpreted as a pattern. A
  process in the given job (or anywhere if not given) whose name matches the
  given pattern will be attached to if it exists, and going forward, new
  processes in said job whose name matches the pattern will be attached to
  automatically. If given a filter as a noun, that filter will be updated.

  When attaching to a job, two switches are accepted to refer to special jobs:

    --root | -r
        Attaches to the system's root job.

    --app | -a
        Attaches to the component manager's job which is the root of all
        components.

  Each job and process can have only one attached debugger system-wide. New
  process notifications are delivered to the most specific attached job (they
  don't "bubble up").

   • Using job filters with multiple debuggers is not advised unless watching
     completely non-overlapping jobs.

   • Even within the same debugger, if there are multiple overapping job
     contexts only the most specific one's filters will apply to a launched
     process.

Hints

  Use the "ps" command to view the active process and job tree.

  To debug more than one process/job at a time, use "new" to create a new
  process ("process new") or job ("job new") context.

Examples

  attach 2371
      Attaches to the process with koid 2371.

  job attach 2323
      Attaches to job with koid 2323.

  job attach -a
      Attaches to the component manager's root job.

  job attach -r
      Attaches to the system's root job.

  process 4 attach 2371
      Attaches process context 4 to the process with koid 2371.

  job 3 attach 2323
      Attaches job context 3 to the job with koid 2323.

  attach foobar
      Attaches to any process that spawns under a job we can see with "foobar"
      in the name.

  job 3 attach foobar
      Attaches to any process that spawns under job 3 with "foobar" in the
      name.

  filter 2 attach foobar
      Change filter 2's pattern so it now matches any process with "foobar" in
      the name.

  filter attach 1234
      Attach to any process that spawns under the current job with "1234" in
      the name.
)";

// Verifies that the given job_context can be run or attached.
Err AssertRunnableJobContext(JobContext* job_context) {
  JobContext::State state = job_context->GetState();
  if (state == JobContext::State::kAttaching) {
    return Err("The current job is in the process of attaching.");
  }
  if (state == JobContext::State::kAttached) {
    return Err(
        "The current job is already attached.\n"
        "Either \"job detach\" it or create a new context with \"job new\".");
  }
  return Err();
}

Err DoAttachFilter(ConsoleContext* context, const Command& cmd,
                   CommandCallback callback = nullptr) {
  if (cmd.args().size() != 1)
    return Err("Wrong number of arguments to attach.");

  Filter* filter;
  if (cmd.HasNoun(Noun::kFilter) && cmd.GetNounIndex(Noun::kFilter) != Command::kNoIndex) {
    if (cmd.HasNoun(Noun::kJob)) {
      return Err("Cannot change job for existing filter.");
    }

    filter = cmd.filter();
  } else {
    JobContext* job = cmd.HasNoun(Noun::kJob) ? cmd.job_context() : nullptr;
    filter = context->session()->system().CreateNewFilter();
    filter->SetJob(job);
  }

  filter->SetPattern(cmd.args()[0]);

  Console::get()->Output("Waiting for process matching \"" + cmd.args()[0] + "\"");
  if (callback) {
    callback(Err());
  }
  return Err();
}

Err RunVerbAttach(ConsoleContext* context, const Command& cmd, CommandCallback callback) {
  // Only a process can be attached.
  if (Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kJob, Noun::kFilter}); err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kJob)) {
    if (Err err = cmd.ValidateNouns({Noun::kJob, Noun::kFilter}); err.has_error())
      return err;

    if (cmd.HasNoun(Noun::kFilter))
      return DoAttachFilter(context, cmd, std::move(callback));

    // Attach a job.
    if (Err err = AssertRunnableJobContext(cmd.job_context()); err.has_error())
      return err;

    auto cb = [callback = std::move(callback)](fxl::WeakPtr<JobContext> job_context,
                                               const Err& err) mutable {
      JobCommandCallback("attach", job_context, true, err, std::move(callback));
    };

    if (cmd.HasSwitch(kAttachComponentRootSwitch) && cmd.HasSwitch(kAttachSystemRootSwitch))
      return Err("Can't specify both component and root job.");

    if (cmd.HasSwitch(kAttachComponentRootSwitch)) {
      if (!cmd.args().empty())
        return Err("No argument expected attaching to the component root.");
      cmd.job_context()->AttachToComponentRoot(std::move(cb));
    } else if (cmd.HasSwitch(kAttachSystemRootSwitch)) {
      if (!cmd.args().empty())
        return Err("No argument expected attaching to the system root.");
      cmd.job_context()->AttachToSystemRoot(std::move(cb));
    } else {
      // Expect a numeric KOID.
      uint64_t koid = 0;
      if (Err err = ReadUint64Arg(cmd, 0, "job koid", &koid); err.has_error())
        return DoAttachFilter(context, cmd, std::move(callback));
      cmd.job_context()->Attach(koid, std::move(cb));
    }
  } else {
    if (cmd.HasNoun(Noun::kFilter)) {
      if (Err err = cmd.ValidateNouns({Noun::kFilter}); err.has_error())
        return err;
      return DoAttachFilter(context, cmd, std::move(callback));
    }

    // Attach a process: Should have one arg which is the koid or PID.
    uint64_t koid = 0;
    if (Err err = ReadUint64Arg(cmd, 0, "process koid", &koid); err.has_error()) {
      // Not a number, make a filter instead.
      if (!cmd.HasNoun(Noun::kProcess)) {
        return DoAttachFilter(context, cmd, std::move(callback));
      }
      return err;
    }

    // Attach to a process by KOID.
    auto err_or_target = GetRunnableTarget(context, cmd);
    if (err_or_target.has_error())
      return err_or_target.err();
    err_or_target.value()->Attach(koid, [callback = std::move(callback)](
                                            fxl::WeakPtr<Target> target, const Err& err) mutable {
      ProcessCommandCallback(target, true, err, std::move(callback));
    });
  }
  return Err();
}

}  // namespace

VerbRecord GetAttachVerbRecord() {
  VerbRecord attach(&RunVerbAttach, {"attach"}, kAttachShortHelp, kAttachHelp,
                    CommandGroup::kProcess);
  attach.switches.push_back(SwitchRecord(kAttachComponentRootSwitch, false, "app", 'a'));
  attach.switches.push_back(SwitchRecord(kAttachSystemRootSwitch, false, "root", 'r'));
  return attach;
}

}  // namespace zxdb
