// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CMD_APP_H_
#define SRC_DEVELOPER_CMD_APP_H_

#include <lib/fit/function.h>

#include <optional>
#include <string>

#include "src/developer/cmd/console.h"
#include "src/developer/cmd/executor.h"

namespace cmd {

class App : public cmd::Console::Client {
 public:
  using QuitCallback = fit::closure;

  struct Options {
    std::optional<std::string> command;
  };

  explicit App(async_dispatcher_t* dispatcher);
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
  zx_status_t OnConsoleCommand(Command command) override;
  void OnConsoleError(zx_status_t status) override;
  void OnConsoleAutocomplete(Autocomplete* autocomplete) override;

 private:
  void Quit();

  QuitCallback quit_callback_;
  Options options_;
  Console console_;
  Executor executor_;
};

}  // namespace cmd

#endif  // SRC_DEVELOPER_CMD_APP_H_
