// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/app.h"

#include <lib/cmdline/args_parser.h>
#include <unistd.h>
#include <zircon/status.h>

#include <memory>
#include <utility>

namespace shell::console {

const char kHelpIntro[] = R"(cliff [-c <command> ]

  A command line interface for Fuchsia.

Options

)";

const char kHelpHelp[] = R"(  --help
  -h
      Prints all command-line switches.)";

const char kCommandHelp[] = R"(  --command
  -c
      Execute the given command.)";

App::App(llcpp::fuchsia::shell::Shell::SyncClient* client, async_dispatcher_t* dispatcher)
    : console_(this, dispatcher, STDIN_FILENO), executor_(client) {}

App::~App() = default;

bool App::Init(int argc, const char** argv, QuitCallback quit_callback) {
  quit_callback_ = std::move(quit_callback);

  cmdline::ArgsParser<Options> parser;
  parser.AddSwitch("command", 'c', kCommandHelp, &Options::command);

  // Special --help switch which doesn't exist in the options structure.
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });

  std::vector<std::string> params;
  cmdline::Status parser_status = parser.Parse(argc, argv, &options_, &params);
  if (parser_status.has_error()) {
    fprintf(stderr, "error: %s\n", parser_status.error_message().c_str());
    return false;
  }

  // Handle --help switch since we're the one that knows about the switches.
  if (requested_help) {
    printf("%s", (kHelpIntro + parser.GetHelp()).c_str());
    Quit();
    return true;
  }

  console_.Init("% ");

  if (options_.command) {
    auto command = std::make_unique<Command>();
    command->Parse(options_.command.value());
    Err status = OnConsoleCommand(std::move(command));
    if (status.code == ZX_ERR_NEXT) {
      Quit();
    }
    return true;
  } else {
    console_.GetNextCommand();
  }
  return true;
}

Err App::OnConsoleCommand(std::unique_ptr<Command> command) {
  if (!command->parse_error().ok()) {
    fprintf(stderr, "error: Invalid command: %s\n", command->parse_error().msg.c_str());
    return Err(ZX_ERR_NEXT, zx_status_get_string(ZX_ERR_NEXT));
  }
  Err status = executor_.Execute(std::move(command), [this]() {
    if (options_.command) {
      Quit();
    } else {
      console_.GetNextCommand();
    }
  });
  if (status.code == ZX_ERR_STOP) {
    Quit();
    return status;
  }
  if (status.code > 0 || (status.code != ZX_ERR_NEXT && status.code != ZX_ERR_ASYNC)) {
    fprintf(stderr, "error: Failed to execute command: %d (%s)\n", status.code, status.msg.c_str());
    return Err(ZX_ERR_NEXT, zx_status_get_string(ZX_ERR_NEXT));
  }
  return status;
}

void App::OnConsoleInterrupt() {
  executor_.KillForegroundTask();
  console_.GetNextCommand();
}

void App::OnConsoleError(zx_status_t status) {
  fprintf(stderr, "error: Failed to read console: %d (%s)\n", status, zx_status_get_string(status));
  Quit();
}

void App::Quit() {
  auto quit_callback = std::move(quit_callback_);
  quit_callback();
}

}  // namespace shell::console
