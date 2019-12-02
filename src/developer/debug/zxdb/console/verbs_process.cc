// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <algorithm>
#include <vector>

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_job.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Makes sure there is a runnable target, creating one if necessary. In the success case, the
// returned target should be used instead of the one from the command (it may be a new one).
ErrOr<Target*> GetRunnableTarget(ConsoleContext* context, const Command& cmd) {
  Target::State state = cmd.target()->GetState();
  if (state == Target::State::kNone)
    return cmd.target();  // Current one is usable.

  if (cmd.GetNounIndex(Noun::kProcess) != Command::kNoIndex) {
    // A process was specified explicitly in the command. Since it's not usable, report an error.
    if (state == Target::State::kStarting || state == Target::State::kAttaching) {
      return Err(
          "The specified process is in the process of starting or attaching.\n"
          "Either \"kill\" it or create a \"new\" process context.");
    }
    return Err(
        "The specified process is already running.\n"
        "Either \"kill\" it or create a \"new\" process context.");
  }

  // Create a new target based on the given one.
  Target* new_target = context->session()->system().CreateNewTarget(cmd.target());
  context->SetActiveTarget(new_target);
  return new_target;
}

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

// Callback for "attach", "detach". The verb affects the message printed to the screen.
void JobCommandCallback(const char* verb, fxl::WeakPtr<JobContext> job_context,
                        bool display_message_on_success, const Err& err,
                        CommandCallback callback = nullptr) {
  if (!display_message_on_success && !err.has_error())
    return;

  Console* console = Console::get();

  OutputBuffer out;
  if (err.has_error()) {
    if (job_context) {
      out.Append(fxl::StringPrintf("Job %d %s failed.\n",
                                   console->context().IdForJobContext(job_context.get()), verb));
    }
    out.Append(err);
  } else if (job_context) {
    out.Append(FormatJobContext(&console->context(), job_context.get()));
  }

  console->Output(out);

  if (callback) {
    callback(err);
  }
}

// Callback for "run", "attach", "detach" and "stop". The verb affects the message printed to the
// screen.
//
// The optional callback parameter will be issued with the error for calling code to identify the
// error.
void ProcessCommandCallback(fxl::WeakPtr<Target> target, bool display_message_on_success,
                            const Err& err, CommandCallback callback = nullptr) {
  if (display_message_on_success || err.has_error()) {
    // Display messaging.
    Console* console = Console::get();

    OutputBuffer out;
    if (err.has_error()) {
      if (target) {
        out.Append(fxl::StringPrintf("Process %d ", console->context().IdForTarget(target.get())));
      }
      out.Append(err);
    } else if (target) {
      out.Append(FormatTarget(&console->context(), target.get()));
    }

    console->Output(out);
  }

  if (callback)
    callback(err);
}

// run ---------------------------------------------------------------------------------------------

constexpr int kRunComponentSwitch = 1;

const char kRunShortHelp[] = "run / r: Run the program.";
const char kRunHelp[] =
    R"(run [--component] [ <program name> <program args>* ]

  Alias: "r"

  Runs the program. With no arguments, "run" will run the binary stored in the
  process context, if any. With an argument, the binary name will be set and
  that binary will be run.

Why "run" is usually wrong

  The following loader environments all have different capabilities (in order
  from least capable to most capable):

    • The debugger's "run <file name>" command (base system process stuff).
    • The system console or "fx shell" (adds some libraries).
    • The base component environment via the shell‘s run and the debugger’s
      "run -c <package url>" (adds component capabilities).
    • The test environment via "fx run-test".
    • The user environment when launched from a “story” (adds high-level
      services like scenic).

  This panoply of environments is why the debugger can't have a simple “run”
  command that always works.

  When the debugger launches a process or a component, that process or
  component will have the same capabilities as the debug_agent running on the
  system. Whether this is enough to run a specific process or component is
  mostly accidental.

  The only way to get the correct environment is to launch your process or
  component in the way it expects and attach the debugger to it. Filters
  allow you to attach to a new process as it's created to debug from the
  beginning. A typical flow is:

    # Register for the process name. Use the name that appears in "ps" for
    # the process:
    [zxdb] attach my_app_name
    Waiting for process matching "my_app_name"

    # Set a pending breakpoint to stop where you want:
    [zxdb] break main
    Breakpoint 1 (Software) on Global, Enabled, stop=All, @ main
    Pending: No matches for location, it will be pending library loads.

    # Launch your app like normal, the debugger should catch it:
    Attached Process 1 [Running] koid=33213 debug_agent.cmx
    🛑 on bp 1 main(…) • main.cc:220
       219 ...
     ▶ 220 int main(int argc, const char* argv[]) {
       221 ...

Arguments

  --component | -c
      Run this program as a component. The program name should be a component
      URL. In addition to the above-discussed limitations, the debugger must
      currently be attached to the system root job.

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

void LaunchComponent(const Command& cmd) {
  debug_ipc::LaunchRequest request;
  request.inferior_type = debug_ipc::InferiorType::kComponent;
  request.argv = cmd.args();

  auto launch_cb = [target = cmd.target()->GetWeakPtr()](const Err& err,
                                                         debug_ipc::LaunchReply reply) {
    if (err.has_error()) {
      Console::get()->Output(err);
      return;
    }
    FXL_DCHECK(reply.inferior_type == debug_ipc::InferiorType::kComponent)
        << "Expected Component, Got: " << debug_ipc::InferiorTypeToString(reply.inferior_type);

    if (reply.status != debug_ipc::kZxOk) {
      // TODO(donosoc): This should interpret the component termination reason values.
      Console::get()->Output(Err("Could not start component %s: %s", reply.process_name.c_str(),
                                 debug_ipc::ZxStatusToString(reply.status)));
      return;
    }

    FXL_DCHECK(target);

    // We tell the session we will be expecting this component.
    FXL_DCHECK(reply.process_id == 0);
    FXL_DCHECK(reply.component_id != 0);
    target->session()->ExpectComponent(reply.component_id);
  };

  cmd.target()->session()->remote_api()->Launch(std::move(request), std::move(launch_cb));
}

Err DoRun(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
  // Only a process can be run.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  // May need to create a new target.
  auto err_or_target = GetRunnableTarget(context, cmd);
  if (err_or_target.has_error())
    return err_or_target.err();
  Target* target = err_or_target.value();

  // Output warning about this possibly not working.
  OutputBuffer warning(Syntax::kWarning, GetExclamation());
  warning.Append(
      " Run won't work for many processes and components. "
      "See \"help run\".\n");
  Console::get()->Output(warning);

  if (!cmd.HasSwitch(kRunComponentSwitch)) {
    if (cmd.args().empty()) {
      // Use the args already set on the target.
      if (target->GetArgs().empty())
        return Err("No program to run. Try \"run <program name>\".");
    } else {
      target->SetArgs(cmd.args());
    }

    target->Launch(
        [callback = std::move(callback)](fxl::WeakPtr<Target> target, const Err& err) mutable {
          // The ConsoleContext displays messages for new processes, so don't display messages when
          // successfully starting.
          ProcessCommandCallback(target, false, err, std::move(callback));
        });
  } else {
    LaunchComponent(cmd);
  }

  return Err();
}

// kill --------------------------------------------------------------------------------------------

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
Err DoKill(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
  // Only a process can be detached.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  if (!cmd.args().empty())
    return Err("The 'kill' command doesn't take any parameters.");

  cmd.target()->Kill(
      [callback = std::move(callback)](fxl::WeakPtr<Target> target, const Err& err) mutable {
        // The ConsoleContext displays messages for stopped processes, so don't display messages
        // when successfully killing.
        ProcessCommandCallback(target, false, err, std::move(callback));
      });
  return Err();
}

// attach ------------------------------------------------------------------------------------------

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

Err DoAttach(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
  // Only a process can be attached.
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kJob, Noun::kFilter});
  if (err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kJob)) {
    Err err = cmd.ValidateNouns({Noun::kJob, Noun::kFilter});
    if (err.has_error())
      return err;

    if (cmd.HasNoun(Noun::kFilter)) {
      return DoAttachFilter(context, cmd, std::move(callback));
    }

    // Attach a job.
    err = AssertRunnableJobContext(cmd.job_context());
    if (err.has_error())
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
      err = ReadUint64Arg(cmd, 0, "job koid", &koid);
      if (err.has_error())
        return DoAttachFilter(context, cmd, std::move(callback));
      cmd.job_context()->Attach(koid, std::move(cb));
    }
  } else {
    if (cmd.HasNoun(Noun::kFilter)) {
      Err err = cmd.ValidateNouns({Noun::kFilter});
      if (err.has_error())
        return err;
      return DoAttachFilter(context, cmd, std::move(callback));
    }

    // Attach a process: Should have one arg which is the koid or PID.
    uint64_t koid = 0;
    err = ReadUint64Arg(cmd, 0, "process koid", &koid);
    if (err.has_error()) {
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

// detach ------------------------------------------------------------------------------------------

// Returns nullptr if there is no target attached to |process_koid|.
Target* SearchForAttachedTarget(ConsoleContext* context, uint64_t process_koid) {
  if (process_koid == 0)
    return nullptr;

  Target* target = nullptr;
  auto targets = context->session()->system().GetTargets();
  for (auto* target_ptr : targets) {
    auto* process = target_ptr->GetProcess();
    if (!process || process->GetKoid() != process_koid)
      continue;

    // We found a target that matches, mark that one as the one that has to detach.
    target = target_ptr;
    break;
  }

  return target;
}

void SendExplicitDetachMessage(ConsoleContext* context, uint64_t process_koid) {
  debug_ipc::DetachRequest request = {};
  request.koid = process_koid;
  request.type = debug_ipc::TaskType::kProcess;

  context->session()->remote_api()->Detach(request, [process_koid](const Err& err,
                                                                   debug_ipc::DetachReply reply) {
    Console* console = Console::get();

    if (err.has_error()) {
      console->Output(err);
      return;
    }

    if (reply.status != debug_ipc::kZxOk) {
      console->Output(Err("Could not detach from process %" PRIu64 ": %s", process_koid,
                          debug_ipc::ZxStatusToString(reply.status)));
      return;
    }

    console->Output(fxl::StringPrintf("Successfully detached from %" PRIu64 ".", process_koid));
  });
}

const char kDetachShortHelp[] = "detach: Detach from a process/job.";
const char kDetachHelp[] =
    R"(detach [pid]

  Detaches the debugger from a running process/job.
  The process will continue running.

Arguments

  pid
      Detach from a process from pid or tell the agent to release an
      uncoordinated process.

      Normally the client and the agent running on Fuchsia are coordinated.
      But there are some cases where the agent will be attached to some
      processes that the client is not aware of. This can happen either when:

      - You are reconnecting to a pre-running agent that was already attached.
      - There are processes waiting on an exception (Just In Time Debugging).

      In both cases, the client is unaware of these processes. Normally upon
      connection zxdb will inform you of these processes and you can query
      those with the "status" command.

      The user can connect to those processes by issuing an attach command or
      it can tell the agent to release them by issuing a detach command. The
      client will first look for any attached processes it is aware of and if
      not it will notify the agent to detach from this "unknown" processes.

Hints

  By default the current process/job is detached.
  To detach a different process/job prefix with "process N" or "job N"

Examples

  detach
      Detaches from the current process.

  detach 1546
      Send a "detach from process 1546" message to the agent. It is not necessary for the client to
      be attached to this process.

  job detach
      Detaches from the current job.

  process 4 detach
      Detaches from process context 4.

  job 3 detach
      Detaches from job context 3.
)";
Err DoDetach(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
  // Only a process can be detached.
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kJob});
  if (err.has_error())
    return err;

  uint64_t process_koid = 0;
  if (cmd.args().size() == 1) {
    if (cmd.HasNoun(Noun::kProcess) || cmd.HasNoun(Noun::kJob))
      return Err(ErrType::kInput, "You can only specify PIDs without context.");
    process_koid = fxl::StringToNumber<uint64_t>(cmd.args()[0]);
  } else if (cmd.args().size() > 1) {
    return Err(ErrType::kInput, "\"detach\" takes at most 1 argument.");
  }

  if (cmd.HasNoun(Noun::kJob)) {
    cmd.job_context()->Detach([callback = std::move(callback)](fxl::WeakPtr<JobContext> job_context,
                                                               const Err& err) mutable {
      JobCommandCallback("detach", job_context, true, err, std::move(callback));
    });
    return Err();
  }


  Target* target = SearchForAttachedTarget(context, process_koid);

  // If there is no suitable target and the user specified a pid to detach to, it means we need to
  // send an explicit detach message.
  if (!target && process_koid != 0) {
    SendExplicitDetachMessage(context, process_koid);
    return Err();

  }

  // Here we either found an attached target or we use the context one (because the user did not
  // specify a process koid to detach from).
  if (!target)
    target = cmd.target();
  // Only print something when there was an error detaching. The console context will watch for
  // Process destruction and print messages for each one in the success case.
  target->Detach(
      [callback = std::move(callback)](fxl::WeakPtr<Target> target, const Err& err) mutable {
        // The ConsoleContext displays messages for stopped processes, so don't display messages
        // when successfully detaching.
        ProcessCommandCallback(target, false, err, std::move(callback));
      });
  return Err();
}

// libs --------------------------------------------------------------------------------------------

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
            [](const debug_ipc::Module& a, const debug_ipc::Module& b) { return a.base < b.base; });

  std::vector<std::vector<std::string>> rows;
  for (const auto& module : modules) {
    rows.push_back(
        std::vector<std::string>{fxl::StringPrintf("0x%" PRIx64, module.base), module.name});
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Load address", 2), ColSpec(Align::kLeft, 0, "Name", 1)},
              rows, &out);
  console->Output(out);
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

// libs --------------------------------------------------------------------------------------------

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

const char kAspaceShortHelp[] = "aspace / as: Show address space for a process.";
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

void OnAspaceComplete(const Err& err, std::vector<debug_ipc::AddressRegion> map) {
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
        fxl::StringPrintf("0x%" PRIx64, region.base + region.size), PrintRegionSize(region.size),
        PrintRegionName(region.depth, region.name)});
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Start", 2), ColSpec(Align::kRight, 0, "End", 2),
               ColSpec(Align::kRight, 0, "Size", 2), ColSpec(Align::kLeft, 0, "Name", 1)},
              rows, &out);

  console->Output(out);
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

// stdout/stderr -----------------------------------------------------------------------------------

const char kStdoutShortHelp[] = "stdout: Show process output.";
const char kStderrShortHelp[] = "stderr: Show process error output.";
const char kStdioHelp[] =
    R"(stdout | stderr

  Shows the stdout/stderr (depending on the command) for a given process.

  zxdb will store the output of a debugged process in a ring buffer in order to
  have it available after the fact. This is independent on whether the output
  is being silenced by the "show-stdout" setting (Run "get" to see the current
  settings, run "help get" and "help set" for more information on settings).

Examples

  // Shows stdout of the current active process.
  stdout
    This is some stdout output.
    This is another stdout output.

  // Shows stderr of process 2.
  pr 2 stderr
    [ERROR] This is a stderr entry.
)";

template <typename ContainerType>
std::string OutputContainer(const ContainerType& container) {
  std::string str;
  str.resize(container.size());
  str.insert(str.end(), container.begin(), container.end());
  return str;
}

Err DoStdio(Verb io_type, const Command& cmd, ConsoleContext* context) {
  FXL_DCHECK(io_type == Verb::kStdout || io_type == Verb::kStderr);

  // Only a process can be specified.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  const char* io_name = io_type == Verb::kStdout ? "stdout" : "stderr";
  if (!cmd.args().empty()) {
    auto msg = fxl::StringPrintf("\"%s\" takes no parameters.", io_name);
    return Err(ErrType::kInput, std::move(msg));
  }

  err = AssertRunningTarget(context, io_name, cmd.target());
  if (err.has_error())
    return err;

  Process* process = cmd.target()->GetProcess();
  auto& container = io_type == Verb::kStdout ? process->get_stdout() : process->get_stderr();
  Console::get()->Output(OutputContainer(container));
  return Err();
}

Err DoStdout(ConsoleContext* context, const Command& cmd) {
  return DoStdio(Verb::kStdout, cmd, context);
}

Err DoStderr(ConsoleContext* context, const Command& cmd) {
  return DoStdio(Verb::kStderr, cmd, context);
}

}  // namespace

void AppendProcessVerbs(std::map<Verb, VerbRecord>* verbs) {
  VerbRecord run(&DoRun, {"run", "r"}, kRunShortHelp, kRunHelp, CommandGroup::kProcess);
  run.switches.push_back(SwitchRecord(kRunComponentSwitch, false, "component", 'c'));
  (*verbs)[Verb::kRun] = std::move(run);

  (*verbs)[Verb::kKill] =
      VerbRecord(&DoKill, {"kill", "k"}, kKillShortHelp, kKillHelp, CommandGroup::kProcess);

  VerbRecord attach(&DoAttach, {"attach"}, kAttachShortHelp, kAttachHelp, CommandGroup::kProcess);
  attach.switches.push_back(SwitchRecord(kAttachComponentRootSwitch, false, "app", 'a'));
  attach.switches.push_back(SwitchRecord(kAttachSystemRootSwitch, false, "root", 'r'));
  (*verbs)[Verb::kAttach] = std::move(attach);

  (*verbs)[Verb::kDetach] =
      VerbRecord(&DoDetach, {"detach"}, kDetachShortHelp, kDetachHelp, CommandGroup::kProcess);
  (*verbs)[Verb::kLibs] =
      VerbRecord(&DoLibs, {"libs"}, kLibsShortHelp, kLibsHelp, CommandGroup::kQuery);
  (*verbs)[Verb::kAspace] =
      VerbRecord(&DoAspace, {"aspace", "as"}, kAspaceShortHelp, kAspaceHelp, CommandGroup::kQuery);
  (*verbs)[Verb::kStdout] =
      VerbRecord(&DoStdout, {"stdout"}, kStdoutShortHelp, kStdioHelp, CommandGroup::kProcess);
  (*verbs)[Verb::kStderr] =
      VerbRecord(&DoStderr, {"stderr"}, kStderrShortHelp, kStdioHelp, CommandGroup::kProcess);
}

}  // namespace zxdb
