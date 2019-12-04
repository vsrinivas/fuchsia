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

  // Gets an output event that was the result of one call to Output() or
  // Clear() on this console. If the event's type field is Type::kOutput, there
  // was an Output() call, and the output field contains the value it was
  // given. If the event's type field is Type::kClear, there was a call to
  // Clear() and the output field is invalid. If the event's type field is
  // Type::kQuitEarly, something interrupted the loop while we were waiting for
  // a call.
  //
  // If no call has happened recently, the message loop will be run until we
  // receive a call or some outside actor causes it to quit.
  OutputEvent GetOutputEvent();

  // Clear any pending output events. That haven't yet been retrieved by
  // GetOutputEvent();
  void FlushOutputEvents();

  // Console implementation
  void Init() override {}
  void Clear() override;
  void Output(const OutputBuffer& output) override;
  void ModalGetOption(const line_input::ModalPromptOptions& options, OutputBuffer message,
                      const std::string& prompt,
                      line_input::ModalLineInput::ModalCompletionCallback cb) override;
  Console::Result ProcessInputLine(const std::string& line,
                                   CommandCallback callback = nullptr) override;

 private:
  Session* session_;
  std::vector<OutputEvent> output_queue_;
  OutputBuffer output_buffer_;
  bool waiting_for_output_ = false;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_MOCK_CONSOLE_H_
