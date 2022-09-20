// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_down.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_frame.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kDownShortHelp[] = "down: Move down the stack";
const char kDownHelp[] =
    R"(down

  Switch the active frame to the one below (forward in time from) the current.

Examples

  down
      Move one frame down the stack

  t 1 down
      Move down the stack on thread 1
)";

void RunVerbDown(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  // Should always be present because we were called synchronously.
  ConsoleContext* console_context = cmd_context->GetConsoleContext();

  if (Err err = AssertStoppedThreadWithFrameCommand(console_context, cmd, "down"); err.has_error())
    return cmd_context->ReportError(err);

  auto id = console_context->GetActiveFrameIdForThread(cmd.thread());
  if (id < 0)
    return cmd_context->ReportError(Err("Cannot find current frame."));

  if (id == 0)
    return cmd_context->ReportError(Err("At bottom of stack."));

  if (cmd.thread()->GetStack().size() == 0)
    return cmd_context->ReportError(Err("No stack frames."));

  id -= 1;

  console_context->SetActiveFrameIdForThread(cmd.thread(), id);
  OutputFrameInfoForChange(cmd_context.get(), cmd.thread()->GetStack()[id], id);
}

}  // namespace

VerbRecord GetDownVerbRecord() {
  return VerbRecord(&RunVerbDown, {"down"}, kDownShortHelp, kDownHelp, CommandGroup::kGeneral);
}

// Shows the given frame for when it changes. This encapsulates the formatting options.
void OutputFrameInfoForChange(CommandContext* cmd_context, const Frame* frame, int id) {
  FormatFrameOptions opts;
  opts.loc.func.name.elide_templates = true;
  opts.loc.func.name.bold_last = true;
  opts.loc.func.params = FormatFunctionNameOptions::kElideParams;

  // Allow eliding of unnecessary path information.
  opts.loc.target_symbols = frame->GetThread()->GetProcess()->GetTarget()->GetSymbols();

  opts.variable.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  opts.variable.pointer_expand_depth = 1;
  opts.variable.max_depth = 4;

  cmd_context->Output(FormatFrame(frame, opts, id));
}

}  // namespace zxdb
