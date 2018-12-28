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
#include <fuchsia/modular/internal/cpp/fidl.h>
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

<command>
add_mod
  Usage: [--story_name=foo] [--mod_name=bar] [--focus_mod=false] [--focus_story=false] add_mod MOD_URL

  Add a new mod or update an existing mod if a mod with --mod_name already
  exists in --story_name.
  Defaults --story_name and --mod_name to MOD_URL.
  Defaults --focus_mod and --focus_story to 'true'.

  MOD_URL
    Mods have a unique "mod_url". It's the mod package's name.
    In BUILD.gn fuchsia_package_name = "mod_url" or mod_url comes from
    flutter_app("mod_url") when there is no fuchsia_package_name set.

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

list_stories
  List all the stories in the current session.

restart_session
  Restarts the current session.)";
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
      FXL_CHECK(std::regex_search(s.service_path, match, name_regex))
          << s.service_path;
      s.name = match[1];
      services->push_back(std::move(s));
    }
    globfree(&globbuf);
  }
}

// Returns a list of all running sessions.
std::vector<DebugService> FindAllSessions() {
  const char kRegex[] = "/sessionmgr.cmx/(\\d+)";
  // See peridot/bin/sessionmgr/sessionmgr_impl.cc's definition of
  // kSessionCtlDir for "sessionctl". These must match.
  std::vector<DebugService> sessions;
  FindDebugServicesForPath("/hub/c/sessionmgr.cmx/*/out/debug/sessionctl",
                           kRegex, &sessions);

  FindDebugServicesForPath(
      "/hub/r/sys/*/c/sessionmgr.cmx/*/out/debug/sessionctl", kRegex,
      &sessions);
  return sessions;
}

PuppetMasterPtr ConnectToPuppetMaster(const DebugService& session) {
  PuppetMasterPtr puppet_master;
  auto request = puppet_master.NewRequest().TakeChannel();
  std::string service_path = session.service_path + "/" + PuppetMaster::Name_;
  if (fdio_service_connect(service_path.c_str(), request.get()) != ZX_OK) {
    FXL_LOG(FATAL) << "Could not connect to PuppetMaster service in "
                   << session.service_path;
  }
  return puppet_master;
}

fuchsia::modular::internal::BasemgrDebugPtr ConnectToBasemgr() {
  fuchsia::modular::internal::BasemgrDebugPtr basemgr;
  auto request = basemgr.NewRequest().TakeChannel();

  std::vector<DebugService> services;
  FindDebugServicesForPath("/hub/c/basemgr/*/out/debug/basemgr", "basemgr",
                           &services);
  // There should only be one basemgr
  FXL_CHECK(services.size() == 1);

  std::string service_path = services[0].service_path;
  if (fdio_service_connect(service_path.c_str(), request.get()) != ZX_OK) {
    FXL_LOG(FATAL) << "Could not connect to basemgr service in "
                   << service_path;
  }

  return basemgr;
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

  auto basemgr = ConnectToBasemgr();

  // To get a PuppetMaster service for a session, use the following code:
  PuppetMasterPtr puppet_master = ConnectToPuppetMaster(sessions[0]);

  modular::SessionCtlApp app(basemgr.get(), puppet_master.get(), logger,
                             loop.dispatcher(), [&loop] { loop.Quit(); });

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
