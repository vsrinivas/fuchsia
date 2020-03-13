// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionctl/session_ctl_app.h"

#include <regex>

#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/bin/sessionctl/session_ctl_constants.h"

namespace modular {

SessionCtlApp::SessionCtlApp(fuchsia::modular::internal::BasemgrDebugPtr basemgr,
                             fuchsia::modular::PuppetMaster* const puppet_master,
                             const modular::Logger& logger, async_dispatcher_t* const dispatcher)
    : basemgr_(std::move(basemgr)),
      puppet_master_(puppet_master),
      logger_(logger),
      dispatcher_(dispatcher) {}

void SessionCtlApp::ExecuteCommand(std::string cmd, const fxl::CommandLine& command_line,
                                   fit::function<void(std::string error)> done) {
  if (cmd == kAddModCommandString) {
    ExecuteAddModCommand(command_line, std::move(done));
  } else if (cmd == kRemoveModCommandString) {
    ExecuteRemoveModCommand(command_line, std::move(done));
  } else if (cmd == kDeleteStoryCommandString) {
    ExecuteDeleteStoryCommand(command_line, std::move(done));
  } else if (cmd == kDeleteAllStoriesCommandString) {
    ExecuteDeleteAllStoriesCommand(std::move(done));
  } else if (cmd == kListStoriesCommandString) {
    ExecuteListStoriesCommand(std::move(done));
  } else if (cmd == kRestartSessionCommandString) {
    ExecuteRestartSessionCommand(std::move(done));
  } else if (cmd == kSelectNextSessionCommandString) {
    ExecuteSelectNextSessionShellCommand(command_line, std::move(done));
  } else if (cmd == kShutdownBasemgrCommandString) {
    ExecuteShutdownBasemgrCommand(command_line, std::move(done));
  } else {
    done(kGetUsageErrorString);
  }
}

void SessionCtlApp::ExecuteRemoveModCommand(const fxl::CommandLine& command_line,
                                            fit::function<void(std::string)> done) {
  if (command_line.positional_args().size() == 1) {
    auto parsing_error = "Missing MOD_NAME. Ex: sessionctl remove_mod slider_mod";
    logger_.LogError(kRemoveModCommandString, parsing_error);
    done(parsing_error);
    return;
  }

  // Get the mod name and default the story name to the mod name's hash
  std::string mod_name = command_line.positional_args().at(1);
  if (mod_name.find(":") == std::string::npos) {
    mod_name = fxl::StringPrintf(kFuchsiaPkgPath, mod_name.c_str(), mod_name.c_str());
  }
  std::string story_name = std::to_string(std::hash<std::string>{}(mod_name));

  // If the story_name flag isn't set, the story name will remain defaulted to
  // the mod name
  command_line.GetOptionValue(kStoryNameFlagString, &story_name);

  auto commands = MakeRemoveModCommands(mod_name);

  std::map<std::string, std::string> params = {{kModNameFlagString, mod_name},
                                               {kStoryNameFlagString, story_name}};

  puppet_master_->ControlStory(story_name, story_puppet_master_.NewRequest());
  PostTaskExecuteStoryCommand(kRemoveModCommandString, std::move(commands), params,
                              std::move(done));
}

void SessionCtlApp::ExecuteAddModCommand(const fxl::CommandLine& command_line,
                                         fit::function<void(std::string)> done) {
  if (command_line.positional_args().size() == 1) {
    auto parsing_error = "Missing MOD_URL. Ex: sessionctl add_mod slider_mod";
    logger_.LogError(kAddModCommandString, parsing_error);
    done(parsing_error);
    return;
  }

  // Get the mod url and default the mod name and story name to the mod url
  std::string mod_url = command_line.positional_args().at(1);
  // If there's not a colon, resolve to the fuchsia package path
  if (mod_url.find(":") == std::string::npos) {
    mod_url = fxl::StringPrintf(kFuchsiaPkgPath, mod_url.c_str(), mod_url.c_str());
  }

  std::string mod_name = mod_url;
  std::string story_name = std::to_string(std::hash<std::string>{}(mod_url));

  if (command_line.HasOption(kStoryNameFlagString)) {
    command_line.GetOptionValue(kStoryNameFlagString, &story_name);
    // regex from src/sys/appmgr/realm.cc:168
    std::regex story_name_regex("[0-9a-zA-Z\\.\\-_:#]+");
    std::smatch story_name_match;
    if (!std::regex_search(story_name, story_name_match, story_name_regex)) {
      auto parsing_error = "Bad characters in story_name: " + story_name;
      logger_.LogError(kStoryNameFlagString, parsing_error);
      done(parsing_error);
      return;
    }
  } else {
    std::cout << "Using auto-generated --story_name value of " << story_name << std::endl;
  }

  command_line.GetOptionValue(kModNameFlagString, &mod_name);
  if (!command_line.HasOption(kModNameFlagString)) {
    std::cout << "Using auto-generated --mod_name value of " << mod_name << std::endl;
  }

  auto commands = MakeAddModCommands(mod_url, mod_name);

  // Focus the mod and story by default
  std::string focus_mod;
  command_line.GetOptionValue(kFocusModFlagString, &focus_mod);
  if (focus_mod == "" || focus_mod == "true") {
    commands.push_back(MakeFocusModCommand(mod_name));
  }

  std::string focus_story;
  command_line.GetOptionValue(kFocusStoryFlagString, &focus_story);
  if (focus_story == "" || focus_story == "true") {
    commands.push_back(MakeFocusStoryCommand());
  }

  std::map<std::string, std::string> params = {{kModUrlFlagString, mod_url},
                                               {kModNameFlagString, mod_name},
                                               {kStoryNameFlagString, story_name}};

  puppet_master_->ControlStory(story_name, story_puppet_master_.NewRequest());
  PostTaskExecuteStoryCommand(kAddModCommandString, std::move(commands), params, std::move(done));
}

void SessionCtlApp::ExecuteDeleteStoryCommand(const fxl::CommandLine& command_line,
                                              fit::function<void(std::string)> done) {
  if (command_line.positional_args().size() == 1) {
    auto parsing_error = "Missing STORY_NAME. Ex. sessionctl delete_story story";
    logger_.LogError(kStoryNameFlagString, parsing_error);
    done(parsing_error);
    return;
  }

  // Get the story name
  std::string story_name = command_line.positional_args().at(1);

  std::map<std::string, std::string> params = {{kStoryNameFlagString, story_name}};
  async::PostTask(dispatcher_, [this, story_name, params, done = std::move(done)]() mutable {
    puppet_master_->GetStories([this, story_name, params, done = std::move(done)](
                                   std::vector<std::string> story_names) mutable {
      auto story_exists = std::find(story_names.begin(), story_names.end(), story_name);
      if (story_exists != story_names.end()) {
        puppet_master_->DeleteStory(story_name, [] {});
        logger_.Log(kDeleteStoryCommandString, params);
      } else {
        done("Non-existent story_name " + story_name);
        return;
      }
      done("");
    });
  });
}

void SessionCtlApp::ExecuteDeleteAllStoriesCommand(fit::function<void(std::string)> done) {
  async::PostTask(dispatcher_, [this, done = std::move(done)]() mutable {
    puppet_master_->GetStories(
        [this, done = std::move(done)](std::vector<std::string> story_names) {
          for (auto story : story_names) {
            puppet_master_->DeleteStory(story, [] {});
          }
          logger_.Log(kDeleteAllStoriesCommandString, std::move(story_names));
          done("");
        });
  });
}

void SessionCtlApp::ExecuteListStoriesCommand(fit::function<void(std::string)> done) {
  async::PostTask(dispatcher_, [this, done = std::move(done)]() mutable {
    puppet_master_->GetStories(
        [this, done = std::move(done)](std::vector<std::string> story_names) {
          logger_.Log(kListStoriesCommandString, std::move(story_names));
          done("");
        });
  });
}

void SessionCtlApp::ExecuteRestartSessionCommand(fit::function<void(std::string)> done) {
  basemgr_->RestartSession([this, done = std::move(done)]() {
    logger_.Log(kRestartSessionCommandString, std::vector<std::string>());
    done("");
  });
}

void SessionCtlApp::ExecuteSelectNextSessionShellCommand(const fxl::CommandLine& command_line,
                                                         fit::function<void(std::string)> done) {
  basemgr_->SelectNextSessionShell([this, done = std::move(done)]() {
    logger_.Log(kSelectNextSessionCommandString, std::vector<std::string>());
    done("");
  });
}

void SessionCtlApp::ExecuteShutdownBasemgrCommand(const fxl::CommandLine& command_line,
                                                  fit::function<void(std::string)> done) {
  if (basemgr_) {
    basemgr_->Shutdown();
    basemgr_.set_error_handler([done = std::move(done)](zx_status_t status) { done(""); });
    return;
  }
  done("Could not find a running basemgr. Is it running?");
}

fuchsia::modular::StoryCommand SessionCtlApp::MakeFocusStoryCommand() {
  fuchsia::modular::StoryCommand command;
  fuchsia::modular::SetFocusState set_focus_state;
  set_focus_state.focused = true;
  command.set_set_focus_state(std::move(set_focus_state));
  return command;
}

fuchsia::modular::StoryCommand SessionCtlApp::MakeFocusModCommand(const std::string& mod_name) {
  fuchsia::modular::StoryCommand command;
  fuchsia::modular::FocusMod focus_mod;
  focus_mod.mod_name_transitional = mod_name;
  command.set_focus_mod(std::move(focus_mod));
  return command;
}

std::vector<fuchsia::modular::StoryCommand> SessionCtlApp::MakeAddModCommands(
    const std::string& mod_url, const std::string& mod_name) {
  fuchsia::modular::Intent intent;
  intent.handler = mod_url;

  std::vector<fuchsia::modular::StoryCommand> commands;
  fuchsia::modular::StoryCommand command;

  // Add command to add or update the mod (it will be updated if the mod_name
  // already exists in the story).
  fuchsia::modular::AddMod add_mod;
  add_mod.mod_name_transitional = mod_name;
  intent.Clone(&add_mod.intent);
  // TODO(MI4-953): Sessionctl takes in inital intent and other fields.

  command.set_add_mod(std::move(add_mod));
  commands.push_back(std::move(command));

  return commands;
}

std::vector<fuchsia::modular::StoryCommand> SessionCtlApp::MakeRemoveModCommands(
    const std::string& mod_name) {
  std::vector<fuchsia::modular::StoryCommand> commands;
  fuchsia::modular::StoryCommand command;

  fuchsia::modular::RemoveMod remove_mod;
  remove_mod.mod_name_transitional = mod_name;
  command.set_remove_mod(std::move(remove_mod));
  commands.push_back(std::move(command));
  return commands;
}

void SessionCtlApp::PostTaskExecuteStoryCommand(
    const std::string command_name, std::vector<fuchsia::modular::StoryCommand> commands,
    std::map<std::string, std::string> params, fit::function<void(std::string)> done) {
  async::PostTask(dispatcher_, [this, command_name, commands = std::move(commands), params,
                                done = std::move(done)]() mutable {
    ExecuteStoryCommand(std::move(commands), params.at(kStoryNameFlagString))
        ->Then([this, command_name, params, done = std::move(done)](bool has_error,
                                                                    std::string result) {
          std::string error = "";
          if (has_error) {
            error = result;
            logger_.LogError(command_name, result);
          } else {
            auto params_copy = params;
            params_copy.emplace(kStoryIdFlagString, result);
            logger_.Log(command_name, params_copy);
          }
          done(error);
        });
  });
}

modular::FuturePtr<bool, std::string> SessionCtlApp::ExecuteStoryCommand(
    std::vector<fuchsia::modular::StoryCommand> commands, const std::string& story_name) {
  story_puppet_master_->Enqueue(std::move(commands));

  auto fut = modular::Future<bool, std::string>::Create("Sessionctl StoryPuppetMaster::Execute");

  story_puppet_master_->Execute([fut](fuchsia::modular::ExecuteResult result) mutable {
    if (result.status == fuchsia::modular::ExecuteStatus::OK) {
      fut->Complete(false, result.story_id->c_str());
    } else {
      std::string error = fxl::StringPrintf("Puppet master returned status: %d and error: %s",
                                            (uint32_t)result.status, result.error_message->c_str());

      FX_LOGS(WARNING) << error << std::endl;
      fut->Complete(true, std::move(error));
    }
  });

  return fut;
}

}  // namespace modular
