// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONCTL_SESSION_CTL_APP_H_
#define SRC_MODULAR_BIN_SESSIONCTL_SESSION_CTL_APP_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/fpromise/result.h>

#include <iostream>
#include <string>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/modular/bin/sessionctl/logger.h"
#include "src/modular/lib/async/cpp/future.h"

using ::fuchsia::modular::PuppetMaster;
using ::fuchsia::modular::PuppetMasterPtr;

namespace modular {

class SessionCtlApp {
 public:
  // A fpromise::error("") will result in command usage being printed. Any other value
  // will result in that error string being printed.
  using CommandResult = fpromise::result<void, std::string>;
  using CommandDoneCallback = fit::function<void(CommandResult)>;

  // Constructs a SessionCtlApp which can read and execute session commands.
  // |basemgr_debug| The BasemgrDebug instance to use to restart sessions.
  // |puppet_master| The interface used to execute commands.
  // |sys_loader| Is used to check if a fuchsia-pkg:// URL references an available package.
  // |logger| The logger used to log the results of commands.
  // |dispatcher| The dispatcher which is used to post the command tasks.
  SessionCtlApp(fuchsia::modular::internal::BasemgrDebugPtr basemgr_debug,
                fuchsia::modular::PuppetMasterPtr puppet_master, fuchsia::sys::LoaderPtr sys_loader,
                const modular::Logger& logger, async_dispatcher_t* const dispatcher);

  // Dispatches the |cmd|. Calls |done| when done with an empty string if
  // execution was successful or with an error string on failure.
  void ExecuteCommand(std::string cmd, const fxl::CommandLine& command_line,
                      CommandDoneCallback done);

 private:
  // Executes the respective command and returns an empty string on success and
  // a string of missing flags on failure.
  void ExecuteAddModCommand(const fxl::CommandLine& command_line, CommandDoneCallback done);
  void ExecuteRemoveModCommand(const fxl::CommandLine& command_line, CommandDoneCallback done);
  void ExecuteDeleteStoryCommand(const fxl::CommandLine& command_line, CommandDoneCallback done);
  void ExecuteDeleteAllStoriesCommand(CommandDoneCallback done);
  void ExecuteListStoriesCommand(CommandDoneCallback done);
  void ExecuteRestartSessionCommand(CommandDoneCallback done);
  void ExecuteShutdownBasemgrCommand(const fxl::CommandLine& command_line,
                                     CommandDoneCallback done);

  void ExecuteAddModCommandInternal(std::string mod_url, const fxl::CommandLine& command_line,
                                    CommandDoneCallback done);

  // Focus the story to which the mod we are adding belongs.
  fuchsia::modular::StoryCommand MakeFocusStoryCommand();

  // Focus the mod we just added. This is not necessary when adding a new mod
  // since it will be always focused. However, when a mod is updated it might
  // not be focused.
  fuchsia::modular::StoryCommand MakeFocusModCommand(const std::string& mod_name);

  std::vector<fuchsia::modular::StoryCommand> MakeAddModCommands(const std::string& mod_url,
                                                                 const std::string& mod_name);

  std::vector<fuchsia::modular::StoryCommand> MakeRemoveModCommands(const std::string& mod_name);

  // Does a PostTask to Execute the commands on StoryPuppetMaster.
  // When the commands are executed do logging and then call
  // on_command_executed_() callback.
  // |command_name| the string command name.
  // |commands| the StoryCommands to execute on StoryPuppetMaster.
  // |params| map of {command_line arg : command_line value}. Used for logging.
  void PostTaskExecuteStoryCommand(const std::string command_name,
                                   std::vector<fuchsia::modular::StoryCommand> commands,
                                   std::map<std::string, std::string> params,
                                   CommandDoneCallback done);

  modular::FuturePtr<bool, std::string> ExecuteStoryCommand(
      std::vector<fuchsia::modular::StoryCommand> commands, const std::string& story_name);

  // Calls |done| with true if |url| identifies a package that is available
  // according to |sys_loader_|.
  void ModPackageExists(std::string url, fit::function<void(bool)> done);

  std::string GenerateMissingFlagString(const std::vector<std::string>& missing_flags);

  fuchsia::modular::internal::BasemgrDebugPtr basemgr_debug_;
  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  fuchsia::sys::LoaderPtr sys_loader_;

  const fxl::CommandLine command_line_;
  const modular::Logger logger_;
  async_dispatcher_t* const dispatcher_;
  fit::function<void()> on_command_executed_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONCTL_SESSION_CTL_APP_H_
