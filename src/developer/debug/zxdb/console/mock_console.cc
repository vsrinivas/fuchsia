// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/mock_console.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_parser.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

void MockConsole::Output(const OutputBuffer& output) {
  output_queue_.push_back({MockConsole::OutputEvent::Type::kOutput, output});
  output_buffer_.Append(output);

  if (waiting_for_output_) {
    waiting_for_output_ = false;
    debug_ipc::MessageLoop::Current()->QuitNow();
  }
}

void MockConsole::Clear() {
  output_queue_.push_back({MockConsole::OutputEvent::Type::kClear, OutputBuffer()});
  output_buffer_.Clear();

  if (waiting_for_output_) {
    waiting_for_output_ = false;
    debug_ipc::MessageLoop::Current()->QuitNow();
  }
}

MockConsole::OutputEvent MockConsole::GetOutputEvent() {
  FXL_DCHECK(!waiting_for_output_);

  if (output_queue_.empty() && debug_ipc::MessageLoop::Current()) {
    waiting_for_output_ = true;
    debug_ipc::MessageLoop::Current()->Run();
  }

  waiting_for_output_ = false;

  if (output_queue_.empty()) {
    return {MockConsole::OutputEvent::Type::kQuitEarly, OutputBuffer()};
  }

  auto ret = std::move(output_queue_.back());
  output_queue_.pop_back();
  return ret;
}

void MockConsole::ModalGetOption(const line_input::ModalPromptOptions& options,
                                 OutputBuffer message, const std::string& prompt,
                                 line_input::ModalLineInput::ModalCompletionCallback cb) {
  // Not implemented in the mock.
  FXL_NOTREACHED();
}

Console::Result MockConsole::ProcessInputLine(const std::string& line, CommandCallback callback) {
  FXL_DCHECK(!line.empty());

  Command cmd;
  auto err = ParseCommand(line, &cmd);

  if (err.has_error()) {
    Console::Output(err);
    return Console::Result::kContinue;
  }

  if (cmd.verb() == Verb::kQuit) {
    return Console::Result::kQuit;
  }

  err = context_.FillOutCommand(&cmd);

  if (err.has_error()) {
    Console::Output(err);
    return Console::Result::kContinue;
  }

  err = DispatchCommand(&context_, cmd, std::move(callback));

  if (cmd.thread() && cmd.verb() != Verb::kNone) {
    // Show the right source/disassembly for the next listing.
    context_.SetSourceAffinityForThread(cmd.thread(), GetVerbRecord(cmd.verb())->source_affinity);
  }

  if (err.has_error()) {
    Console::Output(err);
  }

  return Console::Result::kContinue;
}

}  // namespace zxdb
