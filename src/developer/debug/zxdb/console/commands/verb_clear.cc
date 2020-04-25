// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_clear.h"

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kClearShortHelp[] = "clear / cl: Clear a breakpoint.";
const char kClearHelp[] =
    R"(clear [ <location> ]

  Alias: "cl"

  By default, "clear" will delete the current active breakpoint. Clear a named
  breakpoint by specifying the breakpoint context for the command, e.g.
  "breakpoint 2 clear"

  If a location is given, the command will instead clear all breakpoints at
  that location. Note that the comparison is performed based on input rather
  than actual address, so "clear main" will not clear breakpoints on "$main".

Location arguments

)" LOCATION_ARG_HELP("clear")
        R"(
See also

  "help break": To create breakpoints.
  "help breakpoint": To manage the current breakpoint context.

Examples

  breakpoint 2 clear
  bp 2 cl
  clear
  cl
)";

Err RunVerbClear(ConsoleContext* context, const Command& cmd) {
  std::vector<Breakpoint*> breakpoints;

  if (Err err = ResolveBreakpointsForModification(cmd, "clear", &breakpoints); err.has_error())
    return err;

  for (Breakpoint* breakpoint : breakpoints) {
    OutputBuffer desc("Deleted ");
    desc.Append(FormatBreakpoint(context, breakpoint, false));

    context->session()->system().DeleteBreakpoint(breakpoint);

    Console::get()->Output(desc);
  }

  return Err();
}

}  // namespace

VerbRecord GetClearVerbRecord() {
  return VerbRecord(&RunVerbClear, {"clear", "cl"}, kClearShortHelp, kClearHelp,
                    CommandGroup::kBreakpoint);
}

}  // namespace zxdb
