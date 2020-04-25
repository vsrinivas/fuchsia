// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_enable.h"

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

const char kEnableShortHelp[] = "enable: Enable a breakpoint.";
const char kEnableHelp[] =
    R"(enable

  By default, "enable" will enable the current active breakpoint. It is the
  opposite of "disable". It can be combined with an explicit breakpoint prefix
  to indicate a specific breakpoint to enable.

  In this way, it is an alias for:

    bp set enabled = true

  If a location is given, the command will instead enable all breakpoints at
  that location. Note that the comparison is performed based on input rather
  than actual address, so "enable main" will not enable breakpoints on "$main".

Location arguments

)" LOCATION_ARG_HELP("enable")
        R"(
See also

  "help break": To create breakpoints.
  "help breakpoint": To manage the current breakpoint context.
  "help disable": To disable breakpoints.

Examples

  breakpoint 2 enable
  bp 2 enable
      Enable a specific breakpoint.

  enable
      Enable the current breakpoint.
)";

Err RunVerbEnable(ConsoleContext* context, const Command& cmd) {
  std::vector<Breakpoint*> breakpoints;

  if (Err err = ResolveBreakpointsForModification(cmd, "enable", &breakpoints); err.has_error())
    return err;

  for (Breakpoint* breakpoint : breakpoints) {
    BreakpointSettings settings = breakpoint->GetSettings();
    settings.enabled = true;

    breakpoint->SetSettings(settings);

    OutputBuffer out("Enabled ");
    out.Append(FormatBreakpoint(context, breakpoint, true));
    Console::get()->Output(out);
  }

  return Err();
}

}  // namespace

VerbRecord GetEnableVerbRecord() {
  return VerbRecord(&RunVerbEnable, {"enable"}, kEnableShortHelp, kEnableHelp,
                    CommandGroup::kBreakpoint);
}

}  // namespace zxdb
