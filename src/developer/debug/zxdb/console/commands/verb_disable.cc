// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_disable.h"

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kDisableShortHelp[] = "disable: Disable a breakpoint.";
const char kDisableHelp[] =
    R"(disable [ <location> ]

  By default, "disable" will disable the current active breakpoint. It is the
  opposite of "enable". It can be combined with an explicit breakpoint prefix
  to indicate a specific breakpoint to disable.

  In this way, it is an alias for:

    bp set enabled = false

  If a location is given, the command will instead disable all breakpoints at
  that location. Note that the comparison is performed based on input rather
  than actual address, so "disable main" will not disable breakpoints on
  "$main".

Location arguments

)" LOCATION_ARG_HELP("disable")
        R"(
See also

  "help break": To create breakpoints.
  "help breakpoint": To manage the current breakpoint context.
  "help enable": To enable breakpoints.

Examples

  breakpoint 2 disable
  bp 2 disable
      Disable a specific breakpoint.

  disable
      Disable the current breakpoint.
)";

Err RunVerbDisable(ConsoleContext* context, const Command& cmd) {
  std::vector<Breakpoint*> breakpoints;

  if (Err err = ResolveBreakpointsForModification(cmd, "disable", &breakpoints); err.has_error())
    return err;

  for (Breakpoint* breakpoint : breakpoints) {
    BreakpointSettings settings = breakpoint->GetSettings();
    settings.enabled = false;

    breakpoint->SetSettings(settings);

    OutputBuffer out("Disabled ");
    out.Append(FormatBreakpoint(context, breakpoint, true));
    Console::get()->Output(out);
  }

  return Err();
}

}  // namespace

VerbRecord GetDisableVerbRecord() {
  return VerbRecord(&RunVerbDisable, {"disable"}, kDisableShortHelp, kDisableHelp,
                    CommandGroup::kBreakpoint);
}

}  // namespace zxdb
