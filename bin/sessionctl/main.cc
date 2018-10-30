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

using ::fuchsia::modular::PuppetMaster;
using ::fuchsia::modular::PuppetMasterPtr;

struct ActiveSession {
  std::string name;
  std::string service_path;
};

// Returns a list of all running sessions.
std::vector<ActiveSession> FindAllSessions() {
  // All sessionmgr processes contain the control interfaces for sessions.
  constexpr char kSessionmgrPrefix[] = "/hub/c/sessionmgr";
  constexpr char kOutDebugPath[] = "out/debug";
  // See peridot/bin/sessionmgr/sessionmgr_impl.cc's definition of
  // kSessionCtlDir.
  constexpr char kSessionCtlDir[] = "sessionctl";
  auto glob_str = fxl::StringPrintf("%s/*/%s/%s", kSessionmgrPrefix,
                                    kOutDebugPath, kSessionCtlDir);

  std::regex name_regex(fxl::StringPrintf("%s/([^/]+)/%s/%s", kSessionmgrPrefix,
                                          kOutDebugPath, kSessionCtlDir));

  glob_t globbuf;
  std::vector<ActiveSession> sessions;

  bool sessionmgr_exists = glob(glob_str.data(), 0, nullptr, &globbuf) == 0;
  if (sessionmgr_exists) {
    for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
      ActiveSession s;
      s.service_path = globbuf.gl_pathv[i];
      std::smatch match;
      FXL_CHECK(std::regex_match(s.service_path, match, name_regex))
          << s.service_path;
      s.name = match[1];
      sessions.push_back(std::move(s));
    }
    globfree(&globbuf);
  }
  return sessions;
}

std::string GetUsage() {
  return R"(sessionctl <flags> <command>
    <flags>
    --story_name=STORY_NAME
    --mod_name=MOD_NAME
    --mod_url=MOD_URL
        mods have a unique "mod_url".
        "mod_url" is the binary field on mod manifest.json
        intent.handler = mod_url means launch this specific mod.
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
        optional: --focus_mod, --focus_story

    remove_mod
      Remove the mod.
        required: --mod_name, --story_name
        optional: --json_out)";
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

  const modular::Logger logger(command_line.HasOption("json_out"));
  auto sessions = FindAllSessions();

  if (sessions.empty()) {
    logger.LogError(
        cmd, "Could not find a running sessionmgr. Is the user logged in?");
    return 1;
  }

  if (!command_line.HasOption("json_out")) {
    std::cout << "Found the following sessions:\n\n";
    for (const auto& session : sessions) {
      std::cout << "\t" << session.name << ": " << session.service_path
                << std::endl;
    }
  }

  // To get a PuppetMaster service for a session, use the following code:
  PuppetMasterPtr puppet_master = ConnectToPuppetMaster(sessions[0]);
  modular::SessionCtlApp app(puppet_master.get(), command_line, &loop, logger);

  std::string error;
  if (cmd == "add_mod") {
    error = app.ExecuteAddModCommand();
  } else if (cmd == "remove_mod") {
    error = app.ExecuteRemoveModCommand();
  } else {
    // Print help if command doesn't match a valid command.
    std::cout << GetUsage() << std::endl;
    return 1;
  }

  if (!error.empty()) {
    return 1;
  }

  loop.Run();

  return 0;
}
