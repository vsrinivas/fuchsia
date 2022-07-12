// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_attach_job.h"

#include "src/developer/debug/zxdb/client/job.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

constexpr int kAttachSystemRootSwitch = 1;

const char kAttachJobShortHelp[] = "attach-job / aj: Watch for process launches in a job.";
const char kAttachJobHelp[] =
    R"(attach-job <job-koid>

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

Arguments

    -r
    --root
        Attaches to the system's root job. No job koid is read.

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

bool IsJobAttachable(Job* job) { return job->state() == Job::State::kNone; }

// Searches for an existing attached job weith the given koid.
Job* GetJobAlreadyAttached(System* system, uint64_t job_koid) {
  for (Job* job : system->GetJobs()) {
    if (job->state() == Job::State::kAttached && job->koid() == job_koid)
      return job;
  }
  return nullptr;
}

Err RunVerbAttachJob(ConsoleContext* context, const Command& cmd, CommandCallback callback) {
  if (Err err = cmd.ValidateNouns({Noun::kJob}); err.has_error())
    return err;

  if (cmd.HasSwitch(kAttachSystemRootSwitch) + cmd.args().size() != 1) {
    return Err("Invalid number of arguments.");
  }

  // Which job to attach to.
  enum AttachToWhat { kAttachSystemRoot, kAttachKoid } attach_to_what;
  uint64_t attach_koid = 0;  // Valid when attach_to_what == kAttachKoid.

  if (cmd.HasSwitch(kAttachSystemRootSwitch)) {
    attach_to_what = kAttachSystemRoot;
  } else {
    // Attach by koid.
    if (Err err = ReadUint64Arg(cmd, 0, "job koid", &attach_koid); err.has_error())
      return err;
    attach_to_what = kAttachKoid;
  }

  // Figure out which job to attach.
  Job* job = nullptr;
  if (int job_index = cmd.GetNounIndex(Noun::kJob); job_index != Command::kNoIndex) {
    // User gave an explicit job to attach, it must be attachable.
    if (!IsJobAttachable(cmd.job()))
      return Err("The requested job is already attached.");
    job = cmd.job();
  } else if (attach_to_what == kAttachKoid &&
             (job = GetJobAlreadyAttached(&context->session()->system(), attach_koid))) {
    // The debugger is already attached to the requested koid, re-use it.
  } else if (cmd.job() && IsJobAttachable(cmd.job())) {
    // Use the current job.
    job = cmd.job();
  } else {
    // Create a new job and set it as the current one.
    job = context->session()->system().CreateNewJob();
    context->SetActiveJob(job);
  }

  auto cb = [callback = std::move(callback)](fxl::WeakPtr<Job> job, const Err& err) mutable {
    JobCommandCallback("attach-job", job, true, err, std::move(callback));
  };

  switch (attach_to_what) {
    case kAttachSystemRoot:
      job->AttachToSystemRoot(std::move(cb));
      break;
    case kAttachKoid:
      // Only attach if it's not already attached. It will be attached already if an existing job
      // attachment was found with the requested koid.
      if (job->state() == Job::State::kNone)
        job->Attach(attach_koid, std::move(cb));
      break;
  }

  return Err();
}

}  // namespace

VerbRecord GetAttachJobVerbRecord() {
  VerbRecord attach_job(&RunVerbAttachJob, {"attach-job", "aj"}, kAttachJobShortHelp,
                        kAttachJobHelp, CommandGroup::kProcess);
  attach_job.switches.push_back(SwitchRecord(kAttachSystemRootSwitch, false, "root", 'r'));
  return attach_job;
}

}  // namespace zxdb
