// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionctl/session_ctl_app.h"
#include "peridot/bin/sessionctl/session_ctl_constants.h"

namespace modular {

SessionCtlApp::SessionCtlApp(
    fuchsia::modular::PuppetMaster* const puppet_master,
    const modular::Logger& logger, async_dispatcher_t* const dispatcher,
    const std::function<void()>& on_command_executed)
    : puppet_master_(puppet_master),
      logger_(logger),
      dispatcher_(dispatcher),
      on_command_executed_(on_command_executed) {}

std::string SessionCtlApp::ExecuteCommand(
    std::string cmd, const fxl::CommandLine& command_line) {
  if (cmd == kAddModCommandString) {
    return ExecuteAddModCommand(command_line);
  } else if (cmd == kRemoveModCommandString) {
    return ExecuteRemoveModCommand(command_line);
  } else if (cmd == kDeleteStoryCommandString) {
    return ExecuteDeleteStoryCommand(command_line);
  } else {
    return kGetUsageErrorString;
  }
}

std::string SessionCtlApp::ExecuteRemoveModCommand(
    const fxl::CommandLine& command_line) {
  std::string story_name;
  std::string mod_name;
  std::vector<std::string> missing_flags;

  if (command_line.HasOption(kStoryNameFlagString)) {
    command_line.GetOptionValue(kStoryNameFlagString, &story_name);
  } else {
    missing_flags.push_back(kStoryNameFlagString);
  }

  if (command_line.HasOption(kModNameFlagString)) {
    command_line.GetOptionValue(kModNameFlagString, &mod_name);
  } else {
    missing_flags.push_back(kModNameFlagString);
  }

  std::string parsing_error = GenerateMissingFlagString(missing_flags);
  if (!parsing_error.empty()) {
    logger_.LogError(kAddModCommandString, parsing_error);
    return parsing_error;
  }

  auto commands = MakeRemoveModCommands(mod_name);

  std::map<std::string, std::string> params = {
      {kModNameFlagString, mod_name}, {kStoryNameFlagString, story_name}};

  puppet_master_->ControlStory(story_name, story_puppet_master_.NewRequest());
  PostTaskExecuteStoryCommand(kRemoveModCommandString, std::move(commands),
                              params);

  return parsing_error;
}

std::string SessionCtlApp::ExecuteAddModCommand(
    const fxl::CommandLine& command_line) {
  std::string mod_url;
  std::string story_name;
  std::string mod_name;

  std::vector<std::string> missing_flags;

  if (command_line.HasOption(kModUrlFlagString)) {
    command_line.GetOptionValue(kModUrlFlagString, &mod_url);
  } else {
    missing_flags.emplace_back(kModUrlFlagString);
  }

  if (command_line.HasOption(kStoryNameFlagString)) {
    command_line.GetOptionValue(kStoryNameFlagString, &story_name);
  } else {
    missing_flags.emplace_back(kStoryNameFlagString);
  }

  if (command_line.HasOption(kModNameFlagString)) {
    command_line.GetOptionValue(kModNameFlagString, &mod_name);
  } else {
    missing_flags.emplace_back(kModNameFlagString);
  }

  std::string parsing_error = GenerateMissingFlagString(missing_flags);
  if (!parsing_error.empty()) {
    logger_.LogError(kAddModCommandString, parsing_error);
    return parsing_error;
  }

  auto commands = MakeAddModCommands(mod_url, mod_name);

  if (command_line.HasOption(kFocusModFlagString)) {
    commands.push_back(MakeFocusModCommand(mod_name));
  }

  if (command_line.HasOption(kFocusStoryFlagString)) {
    commands.push_back(MakeFocusStoryCommand());
  }

  std::map<std::string, std::string> params = {
      {kModUrlFlagString, mod_url},
      {kModNameFlagString, mod_name},
      {kStoryNameFlagString, story_name}};

  puppet_master_->ControlStory(story_name, story_puppet_master_.NewRequest());
  PostTaskExecuteStoryCommand(kAddModCommandString, std::move(commands),
                              params);

  return parsing_error;
}

std::string SessionCtlApp::ExecuteDeleteStoryCommand(
    const fxl::CommandLine& command_line) {
  std::string story_name;
  std::vector<std::string> missing_flags;

  if (command_line.HasOption(kStoryNameFlagString)) {
    command_line.GetOptionValue(kStoryNameFlagString, &story_name);
  } else {
    missing_flags.emplace_back(kStoryNameFlagString);
  }

  std::string parsing_error = GenerateMissingFlagString(missing_flags);
  if (!parsing_error.empty()) {
    logger_.LogError(kDeleteStoryCommandString, parsing_error);
    return parsing_error;
  }

  std::map<std::string, std::string> params = {
      {kStoryNameFlagString, story_name}};

  async::PostTask(dispatcher_, [this, story_name, params]() mutable {
    puppet_master_->DeleteStory(story_name, [this, params] {
      logger_.Log(kDeleteStoryCommandString, params);
      on_command_executed_();
    });
  });

  return parsing_error;
}

fuchsia::modular::StoryCommand SessionCtlApp::MakeFocusStoryCommand() {
  fuchsia::modular::StoryCommand command;
  fuchsia::modular::SetFocusState set_focus_state;
  set_focus_state.focused = true;
  command.set_set_focus_state(std::move(set_focus_state));
  return command;
}

fuchsia::modular::StoryCommand SessionCtlApp::MakeFocusModCommand(
    const std::string& mod_name) {
  fuchsia::modular::StoryCommand command;
  fuchsia::modular::FocusMod focus_mod;
  focus_mod.mod_name.push_back(mod_name);
  command.set_focus_mod(std::move(focus_mod));
  return command;
}

fidl::VectorPtr<fuchsia::modular::StoryCommand>
SessionCtlApp::MakeAddModCommands(const std::string& mod_url,
                                  const std::string& mod_name) {
  fuchsia::modular::Intent intent;
  intent.handler = mod_url;

  fidl::VectorPtr<fuchsia::modular::StoryCommand> commands;
  fuchsia::modular::StoryCommand command;

  // Add command to add or update the mod (it will be updated if the mod_name
  // already exists in the story).
  fuchsia::modular::AddMod add_mod;
  add_mod.mod_name.push_back(mod_name);
  intent.Clone(&add_mod.intent);
  // TODO(MI4-953): Sessionctl takes in inital intent and other fields.

  command.set_add_mod(std::move(add_mod));
  commands.push_back(std::move(command));

  return commands;
}

fidl::VectorPtr<fuchsia::modular::StoryCommand>
SessionCtlApp::MakeRemoveModCommands(const std::string& mod_name) {
  fidl::VectorPtr<fuchsia::modular::StoryCommand> commands;
  fuchsia::modular::StoryCommand command;

  fuchsia::modular::RemoveMod remove_mod;
  remove_mod.mod_name.push_back(mod_name);
  command.set_remove_mod(std::move(remove_mod));
  commands.push_back(std::move(command));
  return commands;
}

void SessionCtlApp::PostTaskExecuteStoryCommand(
    const std::string command_name,
    fidl::VectorPtr<fuchsia::modular::StoryCommand> commands,
    std::map<std::string, std::string> params) {
  async::PostTask(dispatcher_, [this, command_name,
                                commands = std::move(commands),
                                params]() mutable {
    ExecuteStoryCommand(std::move(commands), params.at(kStoryNameFlagString))
        ->Then(
            [this, command_name, params](bool has_error, std::string result) {
              if (has_error) {
                logger_.LogError(command_name, result);
              } else {
                auto params_copy = params;
                params_copy.emplace(kStoryIdFlagString, result);
                logger_.Log(command_name, params_copy);
              }
              on_command_executed_();
            });
  });
}

modular::FuturePtr<bool, std::string> SessionCtlApp::ExecuteStoryCommand(
    fidl::VectorPtr<fuchsia::modular::StoryCommand> commands,
    const std::string& story_name) {
  story_puppet_master_->Enqueue(std::move(commands));

  auto fut = modular::Future<bool, std::string>::Create(
      "Sessionctl StoryPuppetMaster::Execute");

  story_puppet_master_->Execute(fxl::MakeCopyable(
      [this, fut](fuchsia::modular::ExecuteResult result) mutable {
        if (result.status == fuchsia::modular::ExecuteStatus::OK) {
          fut->Complete(false, result.story_id->c_str());
        } else {
          std::string error = fxl::StringPrintf(
              "Puppet master returned status: %d and error: %s",
              (uint32_t)result.status, result.error_message->c_str());

          FXL_LOG(WARNING) << error << std::endl;
          fut->Complete(true, std::move(error));
        }
      }));

  return fut;
}

std::string SessionCtlApp::GenerateMissingFlagString(
    const std::vector<std::string>& missing_flags) {
  std::string error;
  if (!missing_flags.empty()) {
    error += "flags missing:";
    for (auto flag : missing_flags) {
      error += " --" + flag;
    }
  }
  return error;
}

}  // namespace modular
