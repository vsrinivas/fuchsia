// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>
#include <algorithm>
#include <vector>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/target.h"
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
    out.OutputErr(err);
  } else if (target) {
    out.Append(DescribeTarget(&console->context(), target.get()));
  }

  console->Output(std::move(out));

  if (callback) {
    callback(err);
  }
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

  A process noun must be specified. Long-term we want to add support to "new"
  multiple things.

Hints

  To see a list of available process contexts, type "process". To switch the
  active process context, specify its index ("process 3").

Example

  This example creates two processes, a "chrome" process, and attaches to
  some existing process.

  [zxdb] run chrome
  Process 1 Running 3456 chrome
  [zxdb] process new
  Process 2 created.
  [zxdb] attach 1239
  Process 2 Running 1239
)";
Err DoNew(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  if (!cmd.HasNoun(Noun::kProcess))
    return Err("Use \"process new\" to create a new process context.");

  Target* new_target =
      context->session()->system().CreateNewTarget(cmd.target());
  context->SetActiveTarget(new_target);
  Console::get()->Output(DescribeTarget(context, new_target));
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

Err DoRun(ConsoleContext* context, const Command& cmd,
          CommandCallback callback = nullptr) {
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
Err DoAttach(ConsoleContext* context, const Command& cmd,
             CommandCallback callback = nullptr) {
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

  cmd.target()->Attach(
      koid, [callback](fxl::WeakPtr<Target> target, const Err& err) {
        ProcessCommandCallback("attach", target, true, err, callback);
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
Err DoDetach(ConsoleContext* context, const Command& cmd,
             CommandCallback callback = nullptr) {
  // Only a process can be detached.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"detach\" takes no parameters.");

  // Only print something when there was an error detaching. The console
  // context will watch for Process destruction and print messages for each one
  // in the success case.
  cmd.target()->Detach([callback](fxl::WeakPtr<Target> target, const Err& err) {
    // The ConsoleContext displays messages for stopped processes, so don't
    // display messages when successfully detaching.
    ProcessCommandCallback("detach", target, false, err, callback);
  });
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
