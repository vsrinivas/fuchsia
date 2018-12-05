// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>
#include <algorithm>
#include <vector>

#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Verifies that the given target can be run or attached.
Err AssertRunnableTarget(Target* target) {
  Target::State state = target->GetState();
  if (state == Target::State::kStarting || state == Target::State::kAttaching) {
    return Err(
        "The current process is in the process of starting or attaching.\n"
        "Either \"kill\" it or create a \"new\" process context.");
  }
  if (state == Target::State::kRunning) {
    return Err(
        "The current process is already running.\n"
        "Either \"kill\" it or create a \"new\" process context.");
  }
  return Err();
}

// Verifies that the given job_context can be run or attached.
Err AssertRunnableJobContext(JobContext* job_context) {
  JobContext::State state = job_context->GetState();
  if (state == JobContext::State::kStarting ||
      state == JobContext::State::kAttaching) {
    return Err(
        "The current job is in the job of starting or attaching.\n"
        "Either \"kill\" it or create a \"new\" job context.");
  }
  if (state == JobContext::State::kRunning) {
    return Err(
        "The current job is already running.\n"
        "Either \"kill\" it or create a \"new\" job context.");
  }
  return Err();
}

// Callback for "attach", "detach". The verb affects the
// message printed to the screen.
void JobCommandCallback(const char* verb, fxl::WeakPtr<JobContext> job_context,
                        bool display_message_on_success, const Err& err,
                        CommandCallback callback = nullptr) {
  if (!display_message_on_success && !err.has_error())
    return;

  Console* console = Console::get();

  OutputBuffer out;
  if (err.has_error()) {
    if (job_context) {
      out.Append(fxl::StringPrintf(
          "Job %d %s failed.\n",
          console->context().IdForJobContext(job_context.get()), verb));
    }
    out.Append(err);
  } else if (job_context) {
    out.Append(DescribeJobContext(&console->context(), job_context.get()));
  }

  console->Output(std::move(out));

  if (callback) {
    callback(err);
  }
}

// Callback for "run", "attach", "detach" and "stop". The verb affects the
// message printed to the screen.
// Since now Verbs commands can take in a callback and Process commands call
// this callback, we optionally pass that callback here to be called in a long
// merry string of callbacks.
void ProcessCommandCallback(const char* verb, fxl::WeakPtr<Target> target,
                            bool display_message_on_success, const Err& err,
                            CommandCallback callback = nullptr) {
  if (!display_message_on_success && !err.has_error())
    return;

  Console* console = Console::get();

  OutputBuffer out;
  if (err.has_error()) {
    if (target) {
      out.Append(fxl::StringPrintf("Process %d %s failed.\n",
                                   console->context().IdForTarget(target.get()),
                                   verb));
    }
    out.Append(err);
  } else if (target) {
    out.Append(DescribeTarget(&console->context(), target.get()));
  }

  console->Output(std::move(out));

  if (callback) {
    callback(err);
  }
}

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

// run -------------------------------------------------------------------------

const char kRunShortHelp[] = "run / r: Run the program.";
const char kRunHelp[] =
    R"(run [ <program name> <program args>* ]

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
  process 2 run
      Runs a process that's already been configured with a binary name.

  run /boot/bin/ps
  run chrome --no-sandbox http://www.google.com/
      Runs the given process.
)";

Err DoRun(ConsoleContext* context, const Command& cmd,
          CommandCallback callback = nullptr) {
  // Only a process can be run.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  err = AssertRunnableTarget(cmd.target());
  if (err.has_error())
    return err;

  if (cmd.args().empty()) {
    // Use the args already set on the target.
    if (cmd.target()->GetArgs().empty())
      return Err("No program to run. Try \"run <program name>\".");
  } else {
    cmd.target()->SetArgs(cmd.args());
  }

  cmd.target()->Launch([callback](fxl::WeakPtr<Target> target, const Err& err) {
    ProcessCommandCallback("launch", target, true, err, callback);
  });
  return Err();
}

// kill ----------------------------------------------------------------------

const char kKillShortHelp[] = "kill / k: terminate a process";
const char kKillHelp[] =
    R"(kill
  Terminates a process from the debugger.
Hints

  By default the current process is detached.
  To detach a different process prefix with "process N"

Examples

  kill
      Kills the current process.

  process 4 kill
      Kills process 4.
)";
Err DoKill(ConsoleContext* context, const Command& cmd,
           CommandCallback callback = nullptr) {
  // Only a process can be detached.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  cmd.target()->Detach([callback](fxl::WeakPtr<Target> target, const Err& err) {
    // The ConsoleContext displays messages for stopped processes, so don't
    // display messages when successfully killing.
    ProcessCommandCallback("kill", target, false, err, callback);
  });
  return Err();
}

// attach ----------------------------------------------------------------------

const char kAttachShortHelp[] = "attach: Attach to a running process/job.";
const char kAttachHelp[] =
    R"(attach <process/job koid>

Hints

  Use the "ps" command to view the active process and job tree.

  To debug more than one process/job at a time, use "new" to create a new
  process/job context.

Examples

  attach 2371
      Attaches to the process with koid 2371.

  job attach 2323
      Attaches to job with koid 2323.

  process 4 attach 2371
      Attaches process context 4 to the process with koid 2371.

  job 3 attach 2323
      Attaches job context 3 to the job with koid 2323.
)";
Err DoAttach(ConsoleContext* context, const Command& cmd,
             CommandCallback callback = nullptr) {
  // Only a process can be attached.
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kJob});
  if (err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kJob)) {
    err = AssertRunnableJobContext(cmd.job_context());
    if (err.has_error())
      return err;

    // Should have one arg which is the koid.
    uint64_t koid = 0;
    err = ReadUint64Arg(cmd, 0, "job koid", &koid);
    if (err.has_error())
      return err;

    cmd.job_context()->Attach(
        koid, [callback](fxl::WeakPtr<JobContext> job_context, const Err& err) {
          JobCommandCallback("attach", job_context, true, err, callback);
        });
  } else {
    err = AssertRunnableTarget(cmd.target());
    if (err.has_error())
      return err;

    // Should have one arg which is the koid.
    uint64_t koid = 0;
    err = ReadUint64Arg(cmd, 0, "process koid", &koid);
    if (err.has_error())
      return err;

    cmd.target()->Attach(
        koid, [callback](fxl::WeakPtr<Target> target, const Err& err) {
          ProcessCommandCallback("attach", target, true, err, callback);
        });
  }
  return Err();
}

// detach ----------------------------------------------------------------------

const char kDetachShortHelp[] = "detach: Detach from a process/job.";
const char kDetachHelp[] =
    R"(detach

  Detaches the debugger from a running process/job. The process will continue
  running.

Hints

  By default the current process/job is detached.
  To detach a different process/job prefix with "process N" or "job N"

Examples

  detach
      Detaches from the current process.

  job detach
      Detaches from the current job.

  process 4 detach
      Detaches from process context 4.

  job 3 detach
      Detaches from job context 3.
)";
Err DoDetach(ConsoleContext* context, const Command& cmd,
             CommandCallback callback = nullptr) {
  // Only a process can be detached.
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kJob});
  if (err.has_error())
    return err;

  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"detach\" takes no parameters.");

  if (cmd.HasNoun(Noun::kJob)) {
    cmd.job_context()->Detach(
        [callback](fxl::WeakPtr<JobContext> job_context, const Err& err) {
          JobCommandCallback("detach", job_context, false, err, callback);
        });
  } else {
    // Only print something when there was an error detaching. The console
    // context will watch for Process destruction and print messages for each
    // one in the success case.
    cmd.target()->Detach(
        [callback](fxl::WeakPtr<Target> target, const Err& err) {
          // The ConsoleContext displays messages for stopped processes, so
          // don't display messages when successfully detaching.
          ProcessCommandCallback("detach", target, false, err, callback);
        });
  }
  return Err();
}

// libs ------------------------------------------------------------------------

const char kLibsShortHelp[] = "libs: Show loaded libraries for a process.";
const char kLibsHelp[] =
    R"(libs

  Shows the loaded library information for the given process.

Examples

  libs
  process 2 libs
)";

// Completion callback for DoLibs().
void OnLibsComplete(const Err& err, std::vector<debug_ipc::Module> modules) {
  Console* console = Console::get();
  if (err.has_error()) {
    console->Output(err);
    return;
  }

  // Sort by load address.
  std::sort(modules.begin(), modules.end(),
            [](const debug_ipc::Module& a, const debug_ipc::Module& b) {
              return a.base < b.base;
            });

  std::vector<std::vector<std::string>> rows;
  for (const auto& module : modules) {
    rows.push_back(std::vector<std::string>{
        fxl::StringPrintf("0x%" PRIx64, module.base), module.name});
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Load address", 2),
               ColSpec(Align::kLeft, 0, "Name", 1)},
              rows, &out);
  console->Output(std::move(out));
}

Err DoLibs(ConsoleContext* context, const Command& cmd) {
  // Only a process can be specified.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"libs\" takes no parameters.");

  err = AssertRunningTarget(context, "libs", cmd.target());
  if (err.has_error())
    return err;

  cmd.target()->GetProcess()->GetModules(&OnLibsComplete);
  return Err();
}

// libs ------------------------------------------------------------------------

std::string PrintRegionSize(uint64_t size) {
  const uint64_t kOneK = 1024u;
  const uint64_t kOneM = kOneK * kOneK;
  const uint64_t kOneG = kOneM * kOneK;
  const uint64_t kOneT = kOneG * kOneK;

  if (size < kOneK)
    return fxl::StringPrintf("%" PRIu64 "B", size);
  if (size < kOneM)
    return fxl::StringPrintf("%" PRIu64 "K", size / kOneK);
  if (size < kOneG)
    return fxl::StringPrintf("%" PRIu64 "M", size / kOneM);
  if (size < kOneT)
    return fxl::StringPrintf("%" PRIu64 "G", size / kOneG);
  return fxl::StringPrintf("%" PRIu64 "T", size / kOneT);
}

std::string PrintRegionName(uint64_t depth, const std::string& name) {
  return std::string(depth * 2, ' ') + name;
}

const char kAspaceShortHelp[] =
    "aspace / as: Show address space for a process.";
const char kAspaceHelp[] =
    R"(aspace [ <address> ]

  Alias: "as"

  Shows the address space map for the given process.

  With no parameters, it shows the entire process address map.
  You can pass a single address and it will show all the regions that
  contain it.

Examples

  aspace
  aspace 0x530b010dc000
  process 2 aspace
)";

void OnAspaceComplete(const Err& err,
                      std::vector<debug_ipc::AddressRegion> map) {
  Console* console = Console::get();
  if (err.has_error()) {
    console->Output(err);
    return;
  }

  if (map.empty()) {
    console->Output("Region not mapped.");
    return;
  }

  std::vector<std::vector<std::string>> rows;
  for (const auto& region : map) {
    rows.push_back(std::vector<std::string>{
        fxl::StringPrintf("0x%" PRIx64, region.base),
        fxl::StringPrintf("0x%" PRIx64, region.base + region.size),
        PrintRegionSize(region.size),
        PrintRegionName(region.depth, region.name)});
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Start", 2),
               ColSpec(Align::kRight, 0, "End", 2),
               ColSpec(Align::kRight, 0, "Size", 2),
               ColSpec(Align::kLeft, 0, "Name", 1)},
              rows, &out);

  console->Output(std::move(out));
}

Err DoAspace(ConsoleContext* context, const Command& cmd) {
  // Only a process can be specified.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  uint64_t address = 0;
  if (cmd.args().size() == 1) {
    err = ReadUint64Arg(cmd, 0, "address", &address);
    if (err.has_error())
      return err;
  } else if (cmd.args().size() > 1) {
    return Err(ErrType::kInput, "\"aspace\" takes zero or one parameter.");
  }

  err = AssertRunningTarget(context, "aspace", cmd.target());
  if (err.has_error())
    return err;

  cmd.target()->GetProcess()->GetAspace(address, &OnAspaceComplete);
  return Err();
}

}  // namespace

void AppendProcessVerbs(std::map<Verb, VerbRecord>* verbs) {
  // TODO(anmittal): Add one for job when we fix verbs.
  (*verbs)[Verb::kNew] = VerbRecord(&DoNew, {"new"}, kNewShortHelp, kNewHelp,
                                    CommandGroup::kProcess);
  (*verbs)[Verb::kRun] = VerbRecord(&DoRun, {"run", "r"}, kRunShortHelp,
                                    kRunHelp, CommandGroup::kProcess);
  (*verbs)[Verb::kKill] = VerbRecord(&DoKill, {"kill", "k"}, kKillShortHelp,
                                     kKillHelp, CommandGroup::kProcess);
  (*verbs)[Verb::kAttach] = VerbRecord(&DoAttach, {"attach"}, kAttachShortHelp,
                                       kAttachHelp, CommandGroup::kProcess);
  (*verbs)[Verb::kDetach] = VerbRecord(&DoDetach, {"detach"}, kDetachShortHelp,
                                       kDetachHelp, CommandGroup::kProcess);
  (*verbs)[Verb::kLibs] = VerbRecord(&DoLibs, {"libs"}, kLibsShortHelp,
                                     kLibsHelp, CommandGroup::kQuery);
  (*verbs)[Verb::kAspace] =
      VerbRecord(&DoAspace, {"aspace", "as"}, kAspaceShortHelp, kAspaceHelp,
                 CommandGroup::kQuery);
}

}  // namespace zxdb
