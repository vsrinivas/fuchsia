#include "peridot/bin/sessionctl/session_ctl_app.h"

namespace modular {

SessionCtlApp::SessionCtlApp(
    fuchsia::modular::PuppetMaster* const puppet_master,
    const fxl::CommandLine& command_line, async::Loop* const loop,
    const modular::Logger& logger)
    : puppet_master_(puppet_master),
      command_line_(command_line),
      loop_(loop),
      logger_(logger) {}

std::string SessionCtlApp::ExecuteRemoveModCommand() {
  std::string story_name;
  std::string mod_name;
  std::vector<std::string> missing_flags;

  if (command_line_.HasOption("story_name")) {
    command_line_.GetOptionValue("story_name", &story_name);
  } else {
    missing_flags.push_back("story_name");
  }

  if (command_line_.HasOption("mod_name")) {
    command_line_.GetOptionValue("mod_name", &mod_name);
  } else {
    missing_flags.push_back("mod_name");
  }

  std::string error = GenerateMissingFlagString(missing_flags);
  if (!error.empty()) {
    logger_.LogError("add_mod", error);
    return error;
  }

  auto commands = MakeRemoveModCommands(mod_name);

  std::map<std::string, std::string> params = {{"mod_name", mod_name},
                                               {"story_name", story_name}};

  puppet_master_->ControlStory(story_name, story_puppet_master_.NewRequest());

  // Get input
  async::PostTask(loop_->dispatcher(), [this, commands = std::move(commands),
                                        params]() mutable {
    ExecuteAction(std::move(commands), params.at("story_name"))
        ->Then([this, params](bool has_error, std::string result) {
          if (has_error) {
            logger_.LogError("remove_mod", result);
          } else {
            auto params_copy = params;
            params_copy.emplace("story_id", result);
            logger_.Log("remove_mod", params_copy);
          }
          loop_->Quit();
        });
  });
  return error;
}

std::string SessionCtlApp::ExecuteAddModCommand() {
  std::string mod_url;
  std::string story_name;
  std::string mod_name;

  std::vector<std::string> missing_flags;

  if (command_line_.HasOption("mod_url")) {
    command_line_.GetOptionValue("mod_url", &mod_url);
  } else {
    missing_flags.emplace_back("mod_url");
  }

  if (command_line_.HasOption("story_name")) {
    command_line_.GetOptionValue("story_name", &story_name);
  } else {
    missing_flags.emplace_back("story_name");
  }

  if (command_line_.HasOption("mod_name")) {
    command_line_.GetOptionValue("mod_name", &mod_name);
  } else {
    missing_flags.emplace_back("mod_name");
  }

  std::string error = GenerateMissingFlagString(missing_flags);
  if (!error.empty()) {
    logger_.LogError("add_mod", error);
    return error;
  }

  auto commands = MakeAddModCommands(mod_url, mod_name);

  if (command_line_.HasOption("focus_mod")) {
    commands.push_back(MakeFocusModCommand(mod_name));
  }

  if (command_line_.HasOption("focus_story")) {
    commands.push_back(MakeFocusStoryCommand());
  }

  std::map<std::string, std::string> params = {
      {"mod_url", mod_url}, {"mod_name", mod_name}, {"story_name", story_name}};

  puppet_master_->ControlStory(story_name, story_puppet_master_.NewRequest());

  // Get input
  async::PostTask(loop_->dispatcher(), [this, commands = std::move(commands),
                                        params]() mutable {
    ExecuteAction(std::move(commands), params.at("story_name"))
        ->Then([this, params](bool has_error, std::string result) {
          if (has_error) {
            logger_.LogError("add_mod", result);
          } else {
            auto params_copy = params;
            params_copy.emplace("story_id", result);
            logger_.Log("add_mod", params_copy);
          }
          loop_->Quit();
        });
  });
  return error;
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

modular::FuturePtr<bool, std::string> SessionCtlApp::ExecuteAction(
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
