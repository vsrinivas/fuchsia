// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_clear.h"

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kClearShortHelp[] = "clear / cl: Clear a breakpoint.";
const char kClearHelp[] =
    R"(clear

  Alias: "cl"

  By itself, "clear" will delete the current active breakpoint.

  Clear a named breakpoint by specifying the breakpoint context for the
  command. Unlike GDB, the context comes first, so instead of "clear 2" to
  clear breakpoint #2, use "breakpoint 2 clear" (or "bp 2 cl" for short).

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
  if (Err err = ValidateNoArgBreakpointModification(cmd, "clear"); err.has_error())
    return err;

  OutputBuffer desc("Deleted ");
  desc.Append(FormatBreakpoint(context, cmd.breakpoint(), false));

  context->session()->system().DeleteBreakpoint(cmd.breakpoint());

  Console::get()->Output(desc);
  return Err();
}

}  // namespace

VerbRecord GetClearVerbRecord() {
  return VerbRecord(&RunVerbClear, {"clear", "cl"}, kClearShortHelp, kClearHelp,
                    CommandGroup::kBreakpoint);
}

}  // namespace zxdb
