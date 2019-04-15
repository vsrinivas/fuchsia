// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/shared/fd_watcher.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/line_input.h"
#include "src/lib/fxl/macros.h"

namespace zxdb {

class OutputBuffer;
class Session;

// The console has some virtual functions for ease of mocking the interface
// for tests.
class Console : public debug_ipc::FDWatcher {
 public:
  explicit Console(Session* session);
  virtual ~Console();

  static Console* get() { return singleton_; }

  ConsoleContext& context() { return context_; }

  fxl::WeakPtr<Console> GetWeakPtr();

  // Prints the first prompt to the screen. This only needs to be called once.
  void Init();

  // Prints the buffer/string to the console.
  void Output(const OutputBuffer& output);
  void Output(const std::string& s);
  void Output(const Err& err);

  // Clears the contents of the console.
  void Clear();

  // The result of dispatching input is either to keep running or quit the
  // message loop to exit.
  enum class Result { kContinue, kQuit };

  // DispatchInputLine will generate the result by parsing the command.
  // Depending on this result, this function could stop the MessageLoop.
  // We pass the result out for callers to use and react accordingly, which
  // can indicate whether they want the console to continue processing
  // commands.
  virtual Result ProcessInputLine(const std::string& line,
                                  CommandCallback callback = nullptr);

 protected:
  Result DispatchInputLine(const std::string& line,
                           CommandCallback callback = nullptr);

  // FDWatcher implementation.
  void OnFDReady(int fd, bool read, bool write, bool err) override;

  // Searches for history at $HOME/.zxdb_history and loads it if found.
  bool SaveHistoryFile();
  void LoadHistoryFile();

  static Console* singleton_;

  ConsoleContext context_;

  debug_ipc::MessageLoop::WatchHandle stdio_watch_;

  LineInputStdout line_input_;

  // Saves the last nonempty input line for re-running when the user just
  // presses "Enter" with no parameters. This must be re-parsed each time
  // because the context can be different.
  std::string previous_line_;

  fxl::WeakPtrFactory<Console> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Console);
};

}  // namespace zxdb
