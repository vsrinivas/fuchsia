// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/mock_console.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_parser.h"
#include "src/developer/debug/zxdb/console/console_suspend_token.h"

namespace zxdb {

void MockConsole::Output(const OutputBuffer& output) {
  output_queue_.push_back({MockConsole::OutputEvent::Type::kOutput, output});
  output_buffer_.Append(output);

  if (waiting_for_output_) {
    waiting_for_output_ = false;
    debug::MessageLoop::Current()->QuitNow();
  }
}

void MockConsole::Clear() {
  output_queue_.push_back({MockConsole::OutputEvent::Type::kClear, OutputBuffer()});
  output_buffer_.Clear();

  if (waiting_for_output_) {
    waiting_for_output_ = false;
    debug::MessageLoop::Current()->QuitNow();
  }
}

MockConsole::OutputEvent MockConsole::GetOutputEvent() {
  FX_DCHECK(!waiting_for_output_);

  if (output_queue_.empty() && debug::MessageLoop::Current()) {
    waiting_for_output_ = true;
    debug::MessageLoop::Current()->Run();
  }

  waiting_for_output_ = false;

  if (output_queue_.empty()) {
    return {MockConsole::OutputEvent::Type::kQuitEarly, OutputBuffer()};
  }

  auto ret = std::move(output_queue_.front());
  output_queue_.erase(output_queue_.begin());
  return ret;
}

void MockConsole::FlushOutputEvents() { output_queue_.clear(); }

bool MockConsole::SendModalReply(const std::string& line) {
  if (!last_modal_cb_)
    return false;

  // Clear callback before issuing the callback so it can issue more if necessary.
  auto cb = std::move(last_modal_cb_);
  cb(line);
  return true;
}

void MockConsole::ModalGetOption(const line_input::ModalPromptOptions& options,
                                 OutputBuffer message, const std::string& prompt,
                                 line_input::ModalLineInput::ModalCompletionCallback cb) {
  // Only one modal prompt is supported at a time by this mock. Otherwise things will get confused
  // when sending the mock reply.
  FX_DCHECK(!last_modal_cb_);
  last_modal_cb_ = std::move(cb);

  // Add the message to the output queue so tests can see what as printed for this prompt.
  Output(message);
}

void MockConsole::ProcessInputLine(const std::string& line, fxl::RefPtr<CommandContext> cmd_context,
                                   bool add_to_history) {
  if (!cmd_context)
    cmd_context = fxl::MakeRefCounted<ConsoleCommandContext>(this);

  FX_DCHECK(!line.empty());

  Command cmd;
  if (Err err = ParseCommand(line, &cmd); err.has_error())
    return cmd_context->ReportError(err);
  if (Err err = context_.FillOutCommand(&cmd); err.has_error())
    return cmd_context->ReportError(err);
  DispatchCommand(cmd, cmd_context);

  if (cmd.thread() && cmd.verb() != Verb::kNone) {
    // Show the right source/disassembly for the next listing.
    context_.SetSourceAffinityForThread(cmd.thread(), GetVerbRecord(cmd.verb())->source_affinity);
  }
}

fxl::RefPtr<ConsoleSuspendToken> MockConsole::SuspendInput() {
  // Mock consoles don't suspend input.
  return fxl::RefPtr<ConsoleSuspendToken>(new ConsoleSuspendToken);
}

void MockConsole::EnableInput() {}

}  // namespace zxdb
