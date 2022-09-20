// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_up.h"

#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/commands/verb_down.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kUpShortHelp[] = "up: Move up the stack";
const char kUpHelp[] =
    R"(up

  Switch the active frame to the one above (backward in time from) the current.

Examples

  up
      Move one frame up the stack

  t 1 up
      Move up the stack on thread 1
)";

void RunVerbUp(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  // Should always be present because we were called synchronously.
  ConsoleContext* console_context = cmd_context->GetConsoleContext();

  if (Err err = AssertStoppedThreadWithFrameCommand(console_context, cmd, "up"); err.has_error())
    return cmd_context->ReportError(err);

  // This computes the frame index from the callback in case the user does "up" faster than an
  // async stack request can complete. Doing the new index computation from the callback ensures
  // that all commands are executed.
  auto on_has_frames = [cmd_context, weak_thread = cmd.thread()->GetWeakPtr()](const Err& err) {
    ConsoleContext* console_context = cmd_context->GetConsoleContext();
    if (!console_context)
      return;  // Console gone, nothing to do.

    if (!weak_thread)
      return cmd_context->ReportError(Err("Thread destroyed."));

    const Thread* thread = weak_thread.get();

    auto id = console_context->GetActiveFrameIdForThread(thread);
    if (id < 0 || thread->GetStack().size() == 0)
      return cmd_context->ReportError(Err("No current frame."));

    id += 1;

    if (static_cast<size_t>(id) >= thread->GetStack().size()) {
      cmd_context->ReportError(Err("At top of stack."));
      return;
    }

    console_context->SetActiveFrameIdForThread(thread, id);
    OutputFrameInfoForChange(cmd_context.get(), thread->GetStack()[id], id);
  };

  if (cmd.thread()->GetStack().has_all_frames()) {
    on_has_frames(Err());
  } else {
    cmd.thread()->GetStack().SyncFrames(std::move(on_has_frames));
  }
}

}  // namespace

VerbRecord GetUpVerbRecord() {
  return VerbRecord(&RunVerbUp, {"up"}, kUpShortHelp, kUpHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
