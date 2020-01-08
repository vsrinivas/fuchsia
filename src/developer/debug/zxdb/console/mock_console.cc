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
  FXL_DCHECK(!last_modal_cb_);
  last_modal_cb_ = std::move(cb);

  // Add the message to the output queue so tests can see what as printed for this prompt.
  Output(message);
}

void MockConsole::ProcessInputLine(const std::string& line, CommandCallback callback) {
  FXL_DCHECK(!line.empty());

  Command cmd;
  auto err = ParseCommand(line, &cmd);

  if (err.has_error()) {
    Console::Output(err);
    return;
  }

  err = context_.FillOutCommand(&cmd);

  if (err.has_error()) {
    Console::Output(err);
    return;
  }

  err = DispatchCommand(&context_, cmd, std::move(callback));

  if (cmd.thread() && cmd.verb() != Verb::kNone) {
    // Show the right source/disassembly for the next listing.
    context_.SetSourceAffinityForThread(cmd.thread(), GetVerbRecord(cmd.verb())->source_affinity);
  }

  if (err.has_error()) {
    Console::Output(err);
  }
}

}  // namespace zxdb
