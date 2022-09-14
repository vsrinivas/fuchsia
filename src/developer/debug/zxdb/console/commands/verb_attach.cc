// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_attach.h"

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

constexpr int kSwitchJob = 1;
constexpr int kSwitchExact = 2;

const char kAttachShortHelp[] = "attach: Attach to processes.";
const char kAttachHelp[] =
    R"(attach [ --job / -j <koid> ] [ --exact ] [ <what> ]

  Attaches to current or future process.

Arguments

    --job <koid>
    -j <koid>
        Only attaching to processes under the job with an id of <koid>. The
        <what> argument can be omitted and all processes under the job will be
        attached.

    --exact
        Attaching to processes with an exact name. The argument will be
        interpreted as a filter that requires an exact match against the process
        name. This bypasses any heuristics below and is useful if the process
        name looks like a koid, a URL, or a moniker.

Attaching to a process by a process id

  Numeric arguments will be interpreted as a process id (koid) that can be used
  to attach to a specific process. For example:

    attach 12345

  This can only attach to existing processes. Use the "ps" command to view all
  active processes, their names, and koids.

Attaching to processes by a component moniker

  Arguments starting with "/" will be interpreted as a component moniker.
  This will create a filter that matches all processes in the component with
  the given moniker.

Attaching to processes by a component URL

  Arguments that look like a URL, e.g., starting with "fuchsia-pkg://" or
  "fuchsia-boot://", will be interpreted as a component URL. This will create a
  filter that matches all processes in components with the given URL.

  NOTE: a component URL could be partial (fxbug.dev/103293) so it's recommended
  to use "attaching by a component name" below.

Attaching to processes by a component name

  Arguments ending with ".cm" will be interpreted as a component name. The
  component name is defined as the base name of the component manifest. So a
  component with an URL "fuchsia-pkg://devhost/foobar#meta/foobar.cm" has a
  name "foobar.cm". This will create a filter that matches all processes in
  components with the given name.

Attaching to processes by a process name

  Other arguments will be interpreted as a general filter which is a substring
  that will be used to matches any part of the process name. Matched processes
  will be attached.

How "attach" works

  Except attaching by a process id, all other "attach" commands will create
  filters. Filters are applied to all processes in the system, both current
  processes and future ones.

  You can:

    • See the current filters with the "filter" command.

    • Delete a filter with "filter [X] rm" where X is the filter index from the
      "filter" list. If no filter index is provided, the current filter will be
      deleted.

    • Change a filter's pattern with "filter [X] set pattern = <newvalue>".

Examples

  attach 2371
      Attaches to the process with koid 2371.

  process 4 attach 2371
      Attaches process context 4 to the process with koid 2371.

  attach foobar
      Attaches to processes with "foobar" in their process names.

  attach /core/foobar
      Attaches to processes in the component /core/foobar.

  attach fuchsia-pkg://devhost/foobar#meta/foobar.cm
      Attaches to processes in components with the above component URL.

  attach foobar.cm
      Attaches to processes in components with the above name.

  attach --exact /pkg/bin/foobar
      Attaches to processes with a name "/pkg/bin/foobar".

  attach --job 2037
      Attaches to all processes under the job with koid 2037.
)";

std::string TrimToZirconMaxNameLength(std::string pattern) {
  if (pattern.size() > kZirconMaxNameLength) {
    Console::get()->Output(OutputBuffer(
        Syntax::kWarning,
        "The filter is trimmed to " + std::to_string(kZirconMaxNameLength) +
            " characters because it's the maximum length for a process name in Zircon."));
    pattern.resize(kZirconMaxNameLength);
  }
  return pattern;
}

Err RunVerbAttach(ConsoleContext* context, const Command& cmd, CommandCallback callback) {
  // Only process can be specified.
  if (Err err = cmd.ValidateNouns({Noun::kProcess}); err.has_error())
    return err;

  // attach <koid> accepts no switch.
  uint64_t koid = 0;
  if (!cmd.HasSwitch(kSwitchJob) && !cmd.HasSwitch(kSwitchExact) &&
      ReadUint64Arg(cmd, 0, "process koid", &koid).ok()) {
    // Check for duplicate koids before doing anything else to avoid creating a container target
    // in this case. It's easy to hit enter twice which will cause a duplicate attach. The
    // duplicate target is the only reason to check here, the attach will fail later if there's
    // a duplicate (say, created in a race condition).
    if (context->session()->system().ProcessFromKoid(koid))
      return Err("Process " + std::to_string(koid) + " is already being debugged.");

    // Attach to a process by KOID.
    auto err_or_target = GetRunnableTarget(context, cmd);
    if (err_or_target.has_error())
      return err_or_target.err();
    err_or_target.value()->Attach(
        koid, [callback = std::move(callback)](fxl::WeakPtr<Target> target, const Err& err,
                                               uint64_t timestamp) mutable {
          // Don't display a message on success because the ConsoleContext will print the new
          // process information when it's detected.
          ProcessCommandCallback(target, false, err, std::move(callback));
        });
    return Err();
  }

  // For all other cases, "process" cannot be specified.
  if (cmd.HasNoun(Noun::kProcess)) {
    return Err("Attaching by filters doesn't support \"process\" noun.");
  }

  // When --job switch is on and --exact is off, require 0 or 1 argument.
  // Otherwise require 1 argument.
  if ((!cmd.HasSwitch(kSwitchJob) || cmd.HasSwitch(kSwitchExact) || !cmd.args().empty()) &&
      cmd.args().size() != 1) {
    return Err("Wrong number of arguments to attach.");
  }

  // --job <koid> must be parsable as uint64.
  uint64_t job_koid = 0;
  if (cmd.HasSwitch(kSwitchJob) &&
      StringToUint64(cmd.GetSwitchValue(kSwitchJob), &job_koid).has_error()) {
    return Err("--job only accepts a koid");
  }

  // Now all the checks are performed. Create a filter.
  Filter* filter = context->session()->system().CreateNewFilter();

  std::string pattern;
  if (!cmd.args().empty())
    pattern = cmd.args()[0];

  if (job_koid) {
    filter->SetJobKoid(job_koid);
  }

  if (cmd.HasSwitch(kSwitchExact)) {
    filter->SetType(debug_ipc::Filter::Type::kProcessName);
    filter->SetPattern(TrimToZirconMaxNameLength(pattern));
  } else if (StringStartsWith(pattern, "fuchsia-pkg://") ||
             StringStartsWith(pattern, "fuchsia-boot://")) {
    filter->SetType(debug_ipc::Filter::Type::kComponentUrl);
    filter->SetPattern(pattern);
  } else if (StringStartsWith(pattern, "/")) {
    filter->SetType(debug_ipc::Filter::Type::kComponentMoniker);
    filter->SetPattern(pattern);
  } else if (StringEndsWith(pattern, ".cm")) {
    filter->SetType(debug_ipc::Filter::Type::kComponentName);
    filter->SetPattern(pattern);
  } else {
    filter->SetType(debug_ipc::Filter::Type::kProcessNameSubstr);
    filter->SetPattern(TrimToZirconMaxNameLength(pattern));
  }

  context->SetActiveFilter(filter);

  // This doesn't use the default filter formatting to try to make it friendlier for people
  // that are less familiar with the debugger and might be unsure what's happening (this is normally
  // one of the first things people do in the debugger. The filter number is usually not relevant
  // anyway.
  if (pattern.empty()) {
    pattern = "job " + cmd.GetSwitchValue(kSwitchJob);
  }
  Console::get()->Output("Waiting for process matching \"" + pattern +
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
  attach.switches.emplace_back(kSwitchJob, true, "job", 'j');
  attach.switches.emplace_back(kSwitchExact, false, "exact");
  return attach;
}

}  // namespace zxdb
