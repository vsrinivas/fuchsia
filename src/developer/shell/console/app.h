// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_CONSOLE_APP_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_APP_H_

#include <lib/fit/function.h>

#include <optional>
#include <string>

#include "src/developer/shell/console/console.h"
#include "src/developer/shell/console/executor.h"

namespace shell::console {

class App : public shell::console::Console::Client {
 public:
  using QuitCallback = fit::closure;

  struct Options {
    std::optional<std::string> command;
  };

  App(llcpp::fuchsia::shell::Shell::SyncClient* client, async_dispatcher_t* dispatcher);
  ~App() override;

  // Initialize the application.
  //
  // The application will begin processing commands on stdin asynchronously
  // using the |dispatcher| provided to the constructor.
  //
  // The application will call |quit_callback| when the the application is done
  // processing commands.
  //
  // Can be called at most once.
  //
  // Returns whether initialization succeeds.
  bool Init(int argc, const char** argv, QuitCallback quit_callback);

  // |cmd::Console::Client| implementation:
  Err OnConsoleCommand(std::unique_ptr<Command> command) override;
  void OnConsoleInterrupt() override;
  void OnConsoleError(zx_status_t status) override;

 private:
  void Quit();

  QuitCallback quit_callback_;
  Options options_;
  Console console_;
  Executor executor_;
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_APP_H_
