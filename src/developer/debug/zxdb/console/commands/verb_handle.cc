// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_handle.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_handle.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

constexpr int kHexSwitch = 1;

const char kHandleShortHelp[] = "handle[s]: Print handle list or details.";
const char kHandleHelp[] =
    R"(handle[s] [ <handle-expression> ]

  With no arguments, prints all handles for the process.

  If an expression is given, the information corresponding to the resulting
  handle value will be printed.

  👉 See "help expressions" for how to write expressions.

Options

  -x
     Print numbers as hexadecimal. Defaults to decimal.

Examples

  handle
  process 1 handles
      Print all handles for the current/given process.

  handle -x h
  handle -x some_object->handle
      Prints the information for the given handle.
)";

void OnEvalComplete(fxl::RefPtr<EvalContext> eval_context, fxl::WeakPtr<Process> weak_process,
                    ErrOrValue value, bool hex) {
  Console* console = Console::get();
  if (!weak_process)
    return console->Output(Err("Process exited while requesting handles."));
  if (value.has_error())
    return console->Output(value.err());

  uint64_t handle_value = 0;
  if (Err err = value.value().PromoteTo64(&handle_value); err.has_error())
    return console->Output(err);

  weak_process->LoadInfoHandleTable([handle_value, hex](
                                        ErrOr<std::vector<debug_ipc::InfoHandleExtended>> handles) {
    Console* console = Console::get();
    if (handles.has_error())
      return console->Output(handles.err());

    // Find the handle in the table.
    for (const auto& handle : handles.value()) {
      if (handle.handle_value == handle_value)
        return console->Output(FormatHandle(handle, hex));
    }
    console->Output("No handle with value " + std::to_string(handle_value) + " in the process.");
  });
}

Err RunVerbHandle(ConsoleContext* context, const Command& cmd) {
  if (Err err = AssertRunningTarget(context, "handle", cmd.target()); err.has_error())
    return err;

  bool hex = cmd.HasSwitch(kHexSwitch);

  if (cmd.args().empty()) {
    cmd.target()->GetProcess()->LoadInfoHandleTable(
        [hex](ErrOr<std::vector<debug_ipc::InfoHandleExtended>> handles) {
          Console* console = Console::get();
          if (handles.has_error())
            return console->Output(handles.err());

          auto handles_sorted = handles.take_value();
          std::sort(
              handles_sorted.begin(), handles_sorted.end(),
              [](const debug_ipc::InfoHandleExtended& a, const debug_ipc::InfoHandleExtended& b) {
                return a.handle_value < b.handle_value;
              });
          console->Output(FormatHandles(handles_sorted, hex));
        });
  } else {
    // Evaluate the expression, then print just that handle.
    fxl::RefPtr<EvalContext> eval_context = GetEvalContextForCommand(cmd);
    return EvalCommandExpression(
        cmd, "handle", eval_context, false, false,
        [eval_context, weak_process = cmd.target()->GetProcess()->GetWeakPtr(),
         hex](ErrOrValue value) {
          OnEvalComplete(eval_context, weak_process, std::move(value), hex);
        });
  }
  return Err();
}

}  // namespace

VerbRecord GetHandleVerbRecord() {
  VerbRecord handle(&RunVerbHandle, {"handle", "handles"}, kHandleShortHelp, kHandleHelp,
                    CommandGroup::kQuery);
  handle.param_type = VerbRecord::kOneParam;
  handle.switches.emplace_back(kHexSwitch, false, "", 'x');
  return handle;
}

}  // namespace zxdb
