// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_MOCK_CONSOLE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_MOCK_CONSOLE_H_

#include <vector>

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class MockConsole : public Console {
 public:
  struct OutputEvent {
    enum class Type {
      kOutput,
      kClear,
      kQuitEarly,
    };

    Type type;
    OutputBuffer output;
  };

  MockConsole(Session* session) : Console(session), session_(session) {}
  virtual ~MockConsole() = default;

  const OutputBuffer& output_buffer() { return output_buffer_; }
  Session* session() { return session_; }

  // Returns true if there are any output events waiting to be read.
  bool HasOutputEvent() const { return !output_queue_.empty(); }

  // Gets an output event that was the result of one call to Output() or Clear() on this console.
  // Output events will be returned in first-in, first-out order.
  //
  // If the event's type field is Type::kOutput, there was an Output() call, and the output field
  // contains the value it was given. If the event's type field is Type::kClear, there was a call to
  // Clear() and the output field is invalid. If the event's type field is Type::kQuitEarly,
  // something interrupted the loop while we were waiting for a call.
  //
  // If no call has happened recently, the message loop will be run until we receive a call or some
  // outside actor causes it to quit.
  OutputEvent GetOutputEvent();

  // Clear any pending output events. That haven't yet been retrieved by
  // GetOutputEvent();
  void FlushOutputEvents();

  // Set to true when Quit() is called.
  bool has_quit() const { return has_quit_; }

  // Completes a pending modal request with the given input. Returns true if it was called, false if
  // there was no pending modal request (tests will want to validate this returned true).
  //
  // This doesn't do validity checking that the input line matches one of the options, unlike the
  // real code path. Tests will want to be careful about sending the right type of input.
  bool SendModalReply(const std::string& line);

  // Console implementation.
  void Init() override {}
  void Quit() override { has_quit_ = true; }
  void Clear() override;
  void Output(const OutputBuffer& output) override;
  void ModalGetOption(const line_input::ModalPromptOptions& options, OutputBuffer message,
                      const std::string& prompt,
                      line_input::ModalLineInput::ModalCompletionCallback cb) override;
  void ProcessInputLine(const std::string& line, fxl::RefPtr<CommandContext> cmd_context = nullptr,
                        bool add_to_history = true) override;

 private:
  Session* session_;
  std::vector<OutputEvent> output_queue_;
  OutputBuffer output_buffer_;
  bool has_quit_ = false;
  bool waiting_for_output_ = false;

  line_input::ModalLineInput::ModalCompletionCallback last_modal_cb_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_MOCK_CONSOLE_H_
