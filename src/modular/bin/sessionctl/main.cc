// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <glob.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <sys/types.h>

#include <chrono>
#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "src/lib/files/file.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/modular/bin/sessionctl/logger.h"
#include "src/modular/bin/sessionctl/session_ctl_app.h"
#include "src/modular/bin/sessionctl/session_ctl_constants.h"
#include "src/modular/lib/async/cpp/future.h"

using ::fuchsia::modular::PuppetMaster;
using ::fuchsia::modular::PuppetMasterPtr;

struct DebugService {
  std::string name;
  std::string service_path;
};

std::string GetUsage() {
  return R"(sessionctl <flags> <command> <argument>
Example:
sessionctl add_mod slider_mod

sessionctl --mod_name=mod1 --story_name=story1 --focus_mod=false
            --focus_story=false add_mod slider_mod

sessionctl --story_name=story1 remove_mod slider_mod

<flags>
--story_name=STORY_NAME
--mod_name=MOD_NAME
--focus_mod=false
    Don't focus the mod.
--focus_story=false
    Don't focus the story.
--json_out
    If flag is set output json for consuming instead of text.
--wait_for_session
    Blocks progress on completing a command until a guest user is logged in.

<command>
add_mod
  Usage: [--story_name=foo] [--mod_name=bar] [--focus_mod=false] [--focus_story=false] add_mod MOD_URL

  Add a new mod or update an existing mod if a mod with --mod_name already
  exists in --story_name.
  Defaults --story_name and --mod_name to MOD_URL.
  Defaults --focus_mod and --focus_story to 'true'.

  MOD_URL
    This can be either the mod's full package url or the mod component's name.
    The mod components name will be converted to the following package url
    format: fuchsia-pkg://fuchsia.com/MOD_URL#meta/MOD_URL.cmx.

  optional: --story_name, --mod_name, --focus_mod, --focus_story

remove_mod
  Usage: [--story_name=foo] remove_mod MOD_NAME

  Removes an existing mod by name. If the mod was added with add_mod,
  use the value you passed to add_mod's --mod_name flag or the default
  value which would be the mod_url.
  Defaults --story_name to MOD_NAME.

  MOD_NAME
    The name of the mod.

  optional: --story_name

delete_story
  Usage: delete_story STORY_NAME

  Deletes the story.

  STORY_NAME
    The name of the story.

delete_all_stories
  Deletes all stories in the current session.

list_stories
  List all the stories in the current session.

login_guest
  Logs in a guest user.

restart_session
  Restarts the current session.

next_session_shell
  Toggles to the next defined session shell.

kill_basemgr
  Shutdown the running instance of basemgr.

help
  Lists the available commands.)";
}

void FindDebugServicesForPath(const char* glob_str, const char* regex_str,
                              std::vector<DebugService>* services) {
  glob_t globbuf;
  bool service_exists = glob(glob_str, 0, nullptr, &globbuf) == 0;
  std::regex name_regex(regex_str);
  if (service_exists) {
    for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
      DebugService s;
      s.service_path = globbuf.gl_pathv[i];
      std::smatch match;
      FXL_CHECK(std::regex_search(s.service_path, match, name_regex)) << s.service_path;
      s.name = match[1];
      services->push_back(std::move(s));
    }
    globfree(&globbuf);
  }
}

// Returns a list of all running sessions.
std::vector<DebugService> FindAllSessions() {
  const char kRegex[] = "/sessionmgr.cmx/(\\d+)";
  // See src/modular/bin/sessionmgr/sessionmgr_impl.cc's definition of
  // kSessionCtlDir for "sessionctl". These must match.
  std::vector<DebugService> sessions;
  FindDebugServicesForPath(modular::kSessionCtlServiceGlobPath, kRegex, &sessions);

  // Path to sessionctl service from a virtual console
  FindDebugServicesForPath("/hub/r/sys/*/c/sessionmgr.cmx/*/out/debug/sessionctl", kRegex,
                           &sessions);
  return sessions;
}

PuppetMasterPtr ConnectToPuppetMaster(const DebugService& session) {
  PuppetMasterPtr puppet_master;
  auto request = puppet_master.NewRequest().TakeChannel();
  std::string service_path = session.service_path + "/" + PuppetMaster::Name_;
  if (fdio_service_connect(service_path.c_str(), request.get()) != ZX_OK) {
    FXL_LOG(FATAL) << "Could not connect to PuppetMaster service in " << session.service_path;
  }
  return puppet_master;
}

fuchsia::modular::internal::BasemgrDebugPtr ConnectToBasemgr() {
  const char kRegex[] = "/basemgr.cmx/(\\d+)";
  std::vector<DebugService> services;
  FindDebugServicesForPath(modular::kBasemgrDebugServiceGlobPath, kRegex, &services);

  // Path to basemgr debug service from a virtual console
  FindDebugServicesForPath("/hub/r/sys/*/c/basemgr.cmx/*/out/debug/basemgr", kRegex, &services);

  if (services.empty()) {
    return nullptr;
  }
  FXL_CHECK(services.size() == 1);
  std::string service_path = services[0].service_path;

  fuchsia::modular::internal::BasemgrDebugPtr basemgr;
  auto request = basemgr.NewRequest().TakeChannel();
  if (fdio_service_connect(service_path.c_str(), request.get()) != ZX_OK) {
    FXL_LOG(FATAL) << "Could not connect to basemgr service in " << service_path;
  }

  return basemgr;
}

// Returns true if a guest user was logged in.
bool LoginAsGuest(bool has_running_sessions, fuchsia::modular::internal::BasemgrDebugPtr basemgr,
                  modular::Logger logger) {
  if (has_running_sessions) {
    logger.LogError(modular::kLoginGuestCommandString,
                    "A user is already logged in. You may log a guest user out "
                    "by running 'sessionctl restart_session' or you may issue "
                    "any other sessionctl command.");
    return false;
  }

  basemgr->LoginAsGuest();
  logger.Log(modular::kLoginGuestCommandString, std::vector<std::string>());
  return true;
}

// Returns true if a guest user was logged in.
bool LoginDefaultGuestUser(fuchsia::modular::internal::BasemgrDebugPtr basemgr,
                           modular::Logger logger, std::vector<DebugService>* sessions,
                           std::string cmd, bool wait_for_session) {
  std::cout << "Logging in as a guest user in the absence of running sessions." << std::endl;
  LoginAsGuest(/*has_running_sessions=*/false, std::move(basemgr), logger);

  do {
    // Wait 2 seconds to allow sessionmgr to initialize
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Finding sessions..." << std::endl;
    *sessions = FindAllSessions();
  } while (wait_for_session && sessions->empty());

  if (sessions->empty()) {
    logger.LogError(cmd,
                    "Unable find a running session after logging in. "
                    "Please try your command again.");
    return false;
  }

  return true;
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);
  const auto& positional_args = command_line.positional_args();
  const auto& cmd = positional_args.empty() ? "" : positional_args[0];

  if (cmd == modular::kHelpCommandString || cmd.empty()) {
    std::cout << GetUsage() << std::endl;
    return 1;
  }

  const modular::Logger logger(command_line.HasOption(modular::kJsonOutFlagString));

  auto basemgr = ConnectToBasemgr();
  if (!basemgr) {
    logger.LogError(cmd, "Could not find a running basemgr. Is it running?");
    return 1;
  }

  auto sessions = FindAllSessions();

  // Continue with log in flow if user issued a login_guest command
  if (cmd == modular::kLoginGuestCommandString) {
    if (LoginAsGuest(/*has_running_sessions=*/!sessions.empty(), std::move(basemgr), logger)) {
      return 0;
    }
    return 1;
  }

  // Log in a guest user if no session is found before continuing to execute
  // the requested command
  if (sessions.empty()) {
    if (cmd == modular::kRestartSessionCommandString) {
      logger.LogError(cmd, "No session to be restarted.");
      return 1;
    }

    bool wait_for_session = command_line.HasOption(modular::kWaitForSessionFlagString);

    // Exit here if no sessions were found after logging in a guest user
    if (!LoginDefaultGuestUser(std::move(basemgr), logger, &sessions, cmd, wait_for_session)) {
      return 1;
    }
  }

  if (!command_line.HasOption(modular::kJsonOutFlagString)) {
    std::cout << "Found the following sessions:\n\n";
    for (const auto& session : sessions) {
      std::cout << "\t" << session.name << ": " << session.service_path << std::endl;
    }
    std::cout << std::endl;
  }

  // To get a PuppetMaster service for a session, use the following code:
  PuppetMasterPtr puppet_master = ConnectToPuppetMaster(sessions[0]);

  modular::SessionCtlApp app(std::move(basemgr), puppet_master.get(), logger, loop.dispatcher());

  app.ExecuteCommand(cmd, command_line, [cmd, logger, &loop](std::string error) {
    if (!error.empty()) {
      if (error == modular::kGetUsageErrorString) {
        // Print help if command doesn't match a valid command.
        std::cout << GetUsage() << std::endl;
      } else {
        logger.LogError(cmd, error);
      }
    }
    loop.Quit();
  });

  loop.Run();

  return 0;
}
