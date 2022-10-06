// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_IMPL_H_

#include <gtest/gtest_prod.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/line_input/modal_line_input.h"

namespace zxdb {

class OutputBuffer;
class Session;

// The console has some virtual functions for ease of mocking the interface for tests.
class ConsoleImpl : public Console {
 public:
  // The line input factory is used to provide a factory for line input implementations that
  // don't interact with the actual stdout for testing purposes. If null, stdout will be used.
  explicit ConsoleImpl(Session* session, line_input::ModalLineInput::Factory line_input_factory =
                                             line_input::ModalLineInput::Factory());
  virtual ~ConsoleImpl();

  fxl::WeakPtr<ConsoleImpl> GetImplWeakPtr();

  // Console implementation
  void Init() override;
  void Quit() override;
  void Output(const OutputBuffer& output) override;
  void Clear() override;
  void ModalGetOption(const line_input::ModalPromptOptions& options, OutputBuffer message,
                      const std::string& prompt,
                      line_input::ModalLineInput::ModalCompletionCallback cb) override;
  void ProcessInputLine(const std::string& line, fxl::RefPtr<CommandContext> cmd_context = nullptr,
                        bool add_to_history = true) override;

 private:
  FRIEND_TEST(ConsoleImplTest, ControlC);

  void DispatchInputLine(const std::string& line, CommandCallback callback = nullptr);

  // Searches for history at $HOME/.zxdb_history and loads it if found.
  bool SaveHistoryFile();
  void LoadHistoryFile();

  debug::MessageLoop::WatchHandle stdio_watch_;

  line_input::ModalLineInput line_input_;

  // Saves the last nonempty input line for re-running when the user just presses "Enter" with no
  // parameters. This must be re-parsed each time because the context can be different.
  std::string previous_line_;

  fxl::WeakPtrFactory<ConsoleImpl> impl_weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ConsoleImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_IMPL_H_
