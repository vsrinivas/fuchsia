// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/future.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/util.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/strings/string_printf.h>
#include "lib/fxl/functional/make_copyable.h"

#include "peridot/bin/sessionctl/logger.h"
#include "peridot/bin/sessionctl/session_ctl_app.h"
#include "peridot/bin/sessionctl/session_ctl_constants.h"

using ::fuchsia::modular::PuppetMaster;
using ::fuchsia::modular::PuppetMasterPtr;

struct ActiveSession {
  std::string name;
  std::string service_path;
};

void FindSessionsForPath(const char* glob_str, const char* regex_str,
                         std::vector<ActiveSession>* sessions) {
  glob_t globbuf;
  bool sessionmgr_exists = glob(glob_str, 0, nullptr, &globbuf) == 0;
  std::regex name_regex(regex_str);
  if (sessionmgr_exists) {
    for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
      ActiveSession s;
      s.service_path = globbuf.gl_pathv[i];
      std::smatch match;
      FXL_CHECK(std::regex_search(s.service_path, match, name_regex))
          << s.service_path;
      s.name = match[1];
      sessions->push_back(std::move(s));
    }
    globfree(&globbuf);
  }
}

// Returns a list of all running sessions.
std::vector<ActiveSession> FindAllSessions() {
  const char kRegex[] = "/sessionmgr/(\\d+)";
  // See peridot/bin/sessionmgr/sessionmgr_impl.cc's definition of
  // kSessionCtlDir for "sessionctl". These must match.
  std::vector<ActiveSession> sessions;
  FindSessionsForPath("/hub/c/sessionmgr/*/out/debug/sessionctl", kRegex,
                      &sessions);

  FindSessionsForPath("/hub/r/sys/*/c/sessionmgr/*/out/debug/sessionctl",
                      kRegex, &sessions);
  return sessions;
}

std::string GetUsage() {
  return R"(sessionctl <flags> <command>
    Example:
    sessionctl --mod_url=slider_mod --mod_name=mod1 --story_name=story1
               --focus_mod --focus_story add_mod

    sessionctl --mod_name=mod1 --story_name=story1 remove_mod

    <flags>
    --story_name=STORY_NAME
    --mod_name=MOD_NAME
    --mod_url=MOD_URL
        mods have a unique "mod_url".
        It is the mod package's name.
        In BUILD.gn fuchsia_package_name = "mod_url" or mod_url comes from
        flutter_app("mod_url") when there is no fuchsia_package_name set.
    --focus_mod
        If flag is set then the mod is focused.
    --focus_story
        If flag is set then the story is focused.
    --json_out
        If flag is set output json for consuming instead of text.

    <command>
    add_mod
      Add new mod or update an existing mod if found.
        required: --story_name, --mod_name, --mod_url
        optional: --focus_mod, --focus_story, --json_out

    remove_mod
      Remove the mod.
        required: --mod_name, --story_name

    delete_story
      Delete the story.
        required: --story_name
        optional: --json_out
    
    list_stories
      List all the stories in the current session.)";
}

PuppetMasterPtr ConnectToPuppetMaster(const ActiveSession& session) {
  PuppetMasterPtr puppet_master;
  auto request = puppet_master.NewRequest().TakeChannel();
  std::string service_path = session.service_path + "/" + PuppetMaster::Name_;
  if (fdio_service_connect(service_path.c_str(), request.get()) != ZX_OK) {
    FXL_LOG(FATAL) << "Could not connect to PuppetMaster service in "
                   << session.service_path;
  }
  return puppet_master;
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);
  const auto& positional_args = command_line.positional_args();
  const auto& cmd = positional_args.empty() ? "" : positional_args[0];

  const modular::Logger logger(
      command_line.HasOption(modular::kJsonOutFlagString));
  auto sessions = FindAllSessions();

  if (sessions.empty()) {
    logger.LogError(
        cmd, "Could not find a running sessionmgr. Is the user logged in?");
    return 1;
  }

  if (!command_line.HasOption(modular::kJsonOutFlagString)) {
    std::cout << "Found the following sessions:\n\n";
    for (const auto& session : sessions) {
      std::cout << "\t" << session.name << ": " << session.service_path
                << std::endl;
    }
    std::cout << std::endl;
  }

  // To get a PuppetMaster service for a session, use the following code:
  PuppetMasterPtr puppet_master = ConnectToPuppetMaster(sessions[0]);
  modular::SessionCtlApp app(puppet_master.get(), logger, loop.dispatcher(),
                             [&loop] { loop.Quit(); });

  std::string parsing_error = app.ExecuteCommand(cmd, command_line);
  if (parsing_error == modular::kGetUsageErrorString) {
    // Print help if command doesn't match a valid command.
    std::cout << GetUsage() << std::endl;
    return 1;
  }

  if (!parsing_error.empty()) {
    return 1;
  }

  loop.Run();

  return 0;
}
