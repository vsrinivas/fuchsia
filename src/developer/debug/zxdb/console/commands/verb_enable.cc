// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_enable.h"

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kEnableShortHelp[] = "enable: Enable a breakpoint.";
const char kEnableHelp[] =
    R"(enable

  By itself, "enable" will enable the current active breakpoint. It is the
  opposite of "disable".

  It can be combined with an explicit breakpoint prefix to indicate a specific
  breakpoint to enable.

  It is an alias for:

    bp set enabled = true

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
  if (Err err = ValidateNoArgBreakpointModification(cmd, "enable"); err.has_error())
    return err;

  BreakpointSettings settings = cmd.breakpoint()->GetSettings();
  settings.enabled = true;

  cmd.breakpoint()->SetSettings(settings);

  OutputBuffer out("Enabled ");
  out.Append(FormatBreakpoint(context, cmd.breakpoint(), true));
  Console::get()->Output(out);

  return Err();
}

}  // namespace

VerbRecord GetEnableVerbRecord() {
  return VerbRecord(&RunVerbEnable, {"enable"}, kEnableShortHelp, kEnableHelp,
                    CommandGroup::kBreakpoint);
}

}  // namespace zxdb
