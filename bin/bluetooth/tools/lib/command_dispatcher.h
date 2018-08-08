// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include <lib/fit/function.h>

#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace bluetooth_tools {

// CommandDispatcher is a mapping between commands (identified by a string and
// arguments) and handler functions that should be executed.
class CommandDispatcher final {
 public:
  CommandDispatcher() = default;

  // Invokes the handler for the command described by |argv|. Returns true if
  // the command was successfully handled, in which case |complete_cb| will be
  // executed asynchronously to signal command completion.
  //
  // Returns false if the command could not be handled, either because |argv|
  // contained invalid arguments or if no handler for the command had been
  // previously registered. |out_cmd_found| will be set to false if no comand
  // handler for this command was registered, true otherwise.
  bool ExecuteCommand(const std::vector<std::string>& argv,
                      fit::closure complete_cb, bool* out_cmd_found);

  // Prints the names of all commands and their descriptions.
  void DescribeAllCommands() const;

  // Each handler is provided with a |command_line| that can be used to obtain
  // positional arguments and options that were passed to the command. If
  // |command_line| returns malformed or invalid arguments, the handler MUST
  // return false. Otherwise if the command can is expressed properly and it is
  // accepted by the handler, the handler MUST return true.
  //
  // Once a command has been executed, |complete_cb| should be called to mark
  // completion the of the command.
  using CommandHandler = fit::function<bool(
      const fxl::CommandLine& command_line, fit::closure complete_cb)>;

  // Registers a handler to be executed for the command |command_name|.
  // |description| is the string that describes the command (to be displayed by
  // DescribedAllCommands()).
  void RegisterHandler(const std::string& command_name,
                       const std::string& description, CommandHandler handler);

  // Returns a list of currently registered command names that start with
  // |prefix|.
  std::vector<std::string> GetCommandsThatMatch(
      const std::string& prefix) const;

 private:
  struct CommandHandlerData {
    CommandHandlerData(const std::string& description, CommandHandler handler);
    CommandHandlerData() = default;

    std::string description;
    CommandHandler handler;
  };
  std::map<std::string, CommandHandlerData> handler_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CommandDispatcher);
};

}  // namespace bluetooth_tools
