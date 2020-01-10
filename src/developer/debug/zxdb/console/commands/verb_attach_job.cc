// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_attach_job.h"

#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/job.h"
#include "src/developer/debug/zxdb/client/job_context.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kAttachComponentRootSwitch = 1;
constexpr int kAttachSystemRootSwitch = 2;

const char kAttachJobShortHelp[] = "attach-job / aj: Watch for process launches in a job.";
const char kAttachJobHelp[] =
    R"(attach-job <job-koid> [ <filter> ]*

  Alias: aj

  A job is a node in the Zircon process tree that contains processes and other
  jobs. Attaching to a job allows watching for process launches in that job and
  all of its sub-jobs.

    • See the current system's job/process tree with the "ps" command.

  The debugger maintains a list of "job contexts" which are numbered starting
  from one. Each can be attached to a Zircon job or not. When referring to a job
  object in the debugger, use the index of the job context.

    • See the current job contexts with the "job" command.
    • Detach a context from a job using "job X detach" where X is the index of
      the job context from the "job" list.

Watching processes in a job

  Filters apply to the processes in attached jobs to determine which new
  processes to attach to. A filter can apply to one specific job or to all
  attached jobs. Without filters, attach-job will just attach to the job but
  will not actually attach to any processes.

    • See the currently filters with the "filter" command.
    • Create a new filter either by adding it as a 2nd argument to "attach-job"
      or later using "attach".
    • Attach to all processes in a job with "attach-job <koid> *". Note that *
      is a special string for filters, regular expressions are not supported.
    • See "help attach" for more on creating filters.

Arguments

  Two switches are accepted to refer to special jobs. With these switches, no
  job koid is read.

    -r
    --root
        Attaches to the system's root job.

    -a
    --app
        Attaches to the component manager's job which is the root of all
        components.

More about jobs

  On startup the debugger will attempt to attach to the system root job. This
  allows filters to apply to all processes in the system without having to
  attach separately to a specific job.

  Each job and process can have only one attached debugger system-wide. New
  process notifications are delivered to the most specific attached job only.
  Permissions can also affect whether the debugger has the ability to see the
  root job so you may find the root job is not attached.

    • Using job filters with multiple debuggers is not advised unless watching
      completely non-overlapping jobs.

    • Even within the same debugger, if there are multiple overapping job
      contexts only the most specific one's filters will apply to a launched
      process.

Examples

  attach-job 12345
      Attaches to the job with koid 12345. Existing filters (if any) will apply.

  attach-job 12345 myprocess
      Attaches to job 12345 and filters for processes inside it with "myprocess"
      in the name.

  attach job 12345
  job 2 attach myprocess    // Assuming the previous command made job context #2.
      Same as the above example but the attach is done with a separate command.

  aj -r
      Attaches to the system root job.

  job 2 detach
  job 2 attach-job 5678
      Detaches the job context #2 from the job it was attached to and then
      attaches it to job 5678.
)";

bool IsJobAttachable(JobContext* job_context) {
  return job_context->GetState() == JobContext::State::kNone;
}

// Searches for an existing attached job weith the given koid.
JobContext* GetJobAlreadyAttached(System* system, uint64_t job_koid) {
  for (JobContext* job_context : system->GetJobContexts()) {
    if (job_context->GetState() == JobContext::State::kAttached &&
        job_context->GetJob()->GetKoid() == job_koid)
      return job_context;
  }
  return nullptr;
}

Err RunVerbAttachJob(ConsoleContext* context, const Command& cmd, CommandCallback callback) {
  if (Err err = cmd.ValidateNouns({Noun::kJob}); err.has_error())
    return err;

  // Index of the first argument that's a filter.
  size_t first_filter_index = 0;

  // Which job to attach to.
  enum AttachToWhat { kAttachComponentRoot, kAttachSystemRoot, kAttachKoid } attach_to_what;
  uint64_t attach_koid = 0;  // Valid when attach_to_what == kAttachKoid.

  if (cmd.HasSwitch(kAttachComponentRootSwitch) && cmd.HasSwitch(kAttachSystemRootSwitch)) {
    return Err("Can't specify both --app and --root.");
  } else if (cmd.HasSwitch(kAttachComponentRootSwitch)) {
    attach_to_what = kAttachComponentRoot;
  } else if (cmd.HasSwitch(kAttachSystemRootSwitch)) {
    attach_to_what = kAttachSystemRoot;
  } else {
    // Attach by koid.
    if (Err err = ReadUint64Arg(cmd, 0, "job koid", &attach_koid); err.has_error())
      return err;
    attach_to_what = kAttachKoid;
    first_filter_index = 1;  // Filters start after the job koid.
  }

  // Figure out which job context to attach.
  JobContext* job_context = nullptr;
  if (int job_index = cmd.GetNounIndex(Noun::kJob); job_index != Command::kNoIndex) {
    // User gave an explicit job to attach, it must be attachable.
    if (!IsJobAttachable(cmd.job_context()))
      return Err("The requested job is already attached.");
    job_context = cmd.job_context();
  } else if (attach_to_what == kAttachKoid &&
             (job_context = GetJobAlreadyAttached(&context->session()->system(), attach_koid))) {
    // The debugger is already attached to the requested koid, re-use it.
  } else if (IsJobAttachable(cmd.job_context())) {
    // Use the current job context.
    job_context = cmd.job_context();
  } else {
    // Create a new job context and set it as the current one.
    job_context = context->session()->system().CreateNewJobContext();
    context->SetActiveJobContext(job_context);
  }

  auto cb = [callback = std::move(callback)](fxl::WeakPtr<JobContext> job_context,
                                             const Err& err) mutable {
    JobCommandCallback("attach-job", job_context, true, err, std::move(callback));
  };

  switch (attach_to_what) {
    case kAttachComponentRoot:
      job_context->AttachToComponentRoot(std::move(cb));
      break;
    case kAttachSystemRoot:
      job_context->AttachToSystemRoot(std::move(cb));
      break;
    case kAttachKoid:
      // Only attach if it's not already attached. It will be attached already if an existing job
      // attachment was found with the requested koid.
      if (job_context->GetState() == JobContext::State::kNone)
        job_context->Attach(attach_koid, std::move(cb));
      break;
  }

  // Create filters attached to this job if requested.
  for (size_t i = first_filter_index; i < cmd.args().size(); i++) {
    Filter* filter = context->session()->system().CreateNewFilter();
    filter->SetJob(job_context);
    filter->SetPattern(cmd.args()[i]);

    context->SetActiveFilter(filter);

    // Output a record of the created filter.
    OutputBuffer out("Created ");
    out.Append(FormatFilter(context, filter));
    Console::get()->Output(out);
  }

  return Err();
}

}  // namespace

VerbRecord GetAttachJobVerbRecord() {
  VerbRecord attach_job(&RunVerbAttachJob, {"attach-job", "aj"}, kAttachJobShortHelp,
                        kAttachJobHelp, CommandGroup::kProcess);
  attach_job.switches.push_back(SwitchRecord(kAttachComponentRootSwitch, false, "app", 'a'));
  attach_job.switches.push_back(SwitchRecord(kAttachSystemRootSwitch, false, "root", 'r'));
  return attach_job;
}

}  // namespace zxdb
