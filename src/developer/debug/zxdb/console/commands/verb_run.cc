// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_run.h"

#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

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

Err RunVerbRun(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
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

}  // namespace

VerbRecord GetRunVerbRecord() {
  VerbRecord run(&RunVerbRun, {"run", "r"}, kRunShortHelp, kRunHelp, CommandGroup::kProcess);
  run.switches.push_back(SwitchRecord(kRunComponentSwitch, false, "component", 'c'));
  return run;
}

}  // namespace zxdb
