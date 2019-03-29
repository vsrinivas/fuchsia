// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONCTL_SESSION_CTL_APP_H_
#define PERIDOT_BIN_SESSIONCTL_SESSION_CTL_APP_H_

#include <iostream>
#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/future.h>
#include <lib/async/cpp/task.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/strings/string_printf.h>
#include "peridot/bin/sessionctl/logger.h"

using ::fuchsia::modular::PuppetMaster;
using ::fuchsia::modular::PuppetMasterPtr;

namespace modular {

class SessionCtlApp {
 public:
  // Constructs a SessionCtlApp which can read and execute session commands.
  // |puppet_master| The interface used to execute commands.
  // |basemgr| The basemgr to use to restart sessions.
  // |command_line| The command line used to read commands and arguments.
  // |logger| The logger used to log the results of commands.
  // |dispatcher| The dispatcher which is used to post the command tasks.
  // |on_command_executed| A callback which is called whenever a command has
  // finished executing.
  explicit SessionCtlApp(
      fuchsia::modular::internal::BasemgrDebug* const basemgr,
      fuchsia::modular::PuppetMaster* const puppet_master,
      const modular::Logger& logger, async_dispatcher_t* const dispatcher,
      fit::function<void()> on_command_executed);

  // Dispatches the |cmd| and returns an empty string on success, "GetUsage" if
  // |cmd| is not valid, and a string of missing flags on failure.
  std::string ExecuteCommand(std::string cmd,
                             const fxl::CommandLine& command_line);

 private:
  // Executes the respective command and returns an empty string on success and
  // a string of missing flags on failure.
  std::string ExecuteAddModCommand(const fxl::CommandLine& command_line);
  std::string ExecuteRemoveModCommand(const fxl::CommandLine& command_line);
  std::string ExecuteDeleteStoryCommand(const fxl::CommandLine& command_line);
  std::string ExecuteDeleteAllStoriesCommand();
  std::string ExecuteListStoriesCommand();
  std::string ExecuteRestartSessionCommand();

  // Focus the story to which the mod we are adding belongs.
  fuchsia::modular::StoryCommand MakeFocusStoryCommand();

  // Focus the mod we just added. This is not necessary when adding a new mod
  // since it will be always focused. However, when a mod is updated it might
  // not be focused.
  fuchsia::modular::StoryCommand MakeFocusModCommand(
      const std::string& mod_name);

  std::vector<fuchsia::modular::StoryCommand> MakeAddModCommands(
      const std::string& mod_url, const std::string& mod_name);

  std::vector<fuchsia::modular::StoryCommand> MakeRemoveModCommands(
      const std::string& mod_name);

  // Does a PostTask to Execute the commands on StoryPuppetMaster.
  // When the commands are executed do logging and then call
  // on_command_executed_() callback.
  // |command_name| the string command name.
  // |commands| the StoryCommands to execute on StoryPuppetMaster.
  // |params| map of {command_line arg : command_line value}. Used for logging.
  void PostTaskExecuteStoryCommand(
      const std::string command_name,
      std::vector<fuchsia::modular::StoryCommand> commands,
      std::map<std::string, std::string> params);

  modular::FuturePtr<bool, std::string> ExecuteStoryCommand(
      std::vector<fuchsia::modular::StoryCommand> commands,
      const std::string& story_name);

  std::string GenerateMissingFlagString(
      const std::vector<std::string>& missing_flags);

  fuchsia::modular::internal::BasemgrDebug* const basemgr_;
  fuchsia::modular::PuppetMaster* const puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  const fxl::CommandLine command_line_;
  const modular::Logger logger_;
  async_dispatcher_t* const dispatcher_;
  fit::function<void()> on_command_executed_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONCTL_SESSION_CTL_APP_H_
