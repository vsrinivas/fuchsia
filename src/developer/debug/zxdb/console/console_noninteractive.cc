// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/console_noninteractive.h"

#include "src/developer/debug/zxdb/console/command_parser.h"

namespace zxdb {

void ConsoleNoninteractive::Quit() { debug::MessageLoop::Current()->QuitNow(); }

void ConsoleNoninteractive::Output(const OutputBuffer& output) { output.WriteToStdout(); }

void ConsoleNoninteractive::ModalGetOption(const line_input::ModalPromptOptions& options,
                                           OutputBuffer message, const std::string& prompt,
                                           line_input::ModalLineInput::ModalCompletionCallback cb) {
  LOGS(Error) << "Modal is not supported in non-interactive console";
  cb(options.cancel_option);
}

void ConsoleNoninteractive::ProcessInputLine(const std::string& line,
                                             fxl::RefPtr<CommandContext> cmd_context,
                                             bool add_to_history) {
  if (line.empty())
    return;

  if (!cmd_context)
    cmd_context = fxl::MakeRefCounted<ConsoleCommandContext>(this);

  Command cmd;

  if (Err err = ParseCommand(line, &cmd); err.has_error())
    return cmd_context->ReportError(err);

  if (Err err = context_.FillOutCommand(&cmd); err.has_error())
    return cmd_context->ReportError(err);

  DispatchCommand(cmd, cmd_context);
}

}  // namespace zxdb
