// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_NONINTERACTIVE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_NONINTERACTIVE_H_

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/line_input/modal_line_input.h"

namespace zxdb {

class OutputBuffer;
class Session;

// A non-interactive console that doesn't read from stdin but write to stdout. This is useful
// if the console is not the user interface, e.g., when zxdb is embedded in a debugger GUI and
// debug adapter protocol is used to interact with zxdb.
class ConsoleNoninteractive : public Console {
 public:
  explicit ConsoleNoninteractive(Session* session) : Console(session) {}
  ~ConsoleNoninteractive() override = default;

  // Console implementation
  void Init() override {}
  void Quit() override;
  void Output(const OutputBuffer& output) override;
  void Clear() override {}
  void ModalGetOption(const line_input::ModalPromptOptions& options, OutputBuffer message,
                      const std::string& prompt,
                      line_input::ModalLineInput::ModalCompletionCallback cb) override;
  void ProcessInputLine(const std::string& line, fxl::RefPtr<CommandContext> cmd_context = nullptr,
                        bool add_to_history = true) override;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_NONINTERACTIVE_H_
