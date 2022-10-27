// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_finish.h"

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kFinishShortHelp[] = "finish / fi: Finish execution of a stack frame.";
const char kFinishHelp[] =
    R"(finish / fi

  Alias: "fi"

  Resume thread execution until the selected stack frame returns. This means
  that the current function call will execute normally until it finished.

  See also "until".

Examples

  fi
  finish
      Exit the currently selected stack frame (see "frame").

  pr 1 t 4 fi
  process 1 thead 4 finish
      Applies "finish" to process 1, thread 4.

  f 2 fi
  frame 2 finish
      Exit frame 2, leaving program execution in what was frame 3. Try also
      "frame 3 until" which will do the same thing when the function is not
      recursive.
)";

void RunVerbFinish(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (Err err =
          AssertStoppedThreadWithFrameCommand(cmd_context->GetConsoleContext(), cmd, "finish");
      err.has_error())
    return cmd_context->ReportError(err);

  Stack& stack = cmd.thread()->GetStack();
  size_t frame_index;
  if (auto found_frame_index = stack.IndexForFrame(cmd.frame())) {
    frame_index = *found_frame_index;
  } else {
    return cmd_context->ReportError(Err("Internal error, frame not found in current thread."));
  }

  auto controller = std::make_unique<FinishThreadController>(
      stack, frame_index, &ScheduleAsyncPrintReturnValue, fit::defer_callback([cmd_context]() {}));
  cmd.thread()->ContinueWith(std::move(controller), [cmd_context](const Err& err) {
    if (err.has_error())
      cmd_context->ReportError(err);
  });
}

}  // namespace

VerbRecord GetFinishVerbRecord() {
  return VerbRecord(&RunVerbFinish, {"finish", "fi"}, kFinishShortHelp, kFinishHelp,
                    CommandGroup::kStep);
}

}  // namespace zxdb
