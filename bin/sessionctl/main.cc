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
#include <lib/fdio/util.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/strings/string_printf.h>

using ::fuchsia::modular::PuppetMaster;
using ::fuchsia::modular::PuppetMasterPtr;

struct ActiveSession {
  std::string name;
  std::string service_path;
};

// Returns a list of all running sessions.
std::vector<ActiveSession> FindAllSessions() {
  // All user_runner processes contain the control interfaces for sessions.
  constexpr char kUserRunnerPrefix[] = "/hub/c/user_runner";
  constexpr char kOutDebugPath[] = "out/debug";
  // See peridot/bin/user_runner/user_runner_impl.cc's definition of
  // kSessionCtlDir.
  constexpr char kSessionCtlDir[] = "sessionctl";
  auto glob_str = fxl::StringPrintf("%s/*/%s/%s", kUserRunnerPrefix,
                                    kOutDebugPath, kSessionCtlDir);

  std::regex name_regex(fxl::StringPrintf("%s/([^/]+)/%s/%s", kUserRunnerPrefix,
                                          kOutDebugPath, kSessionCtlDir));

  glob_t globbuf;
  std::vector<ActiveSession> sessions;
  FXL_CHECK(glob(glob_str.data(), 0, NULL, &globbuf) == 0);
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
  return sessions;
}

PuppetMasterPtr ConnectToPuppetMaster(const ActiveSession& session) {
  PuppetMasterPtr puppet_master;
  auto request = puppet_master.NewRequest().TakeChannel();
  std::string service_path =
      session.service_path + "/" + PuppetMaster::Name_;
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

  auto sessions = FindAllSessions();
  FXL_CHECK(!sessions.empty())
      << "Could not find a running user_runner. Is the user logged in?";

  std::cout << "Found the following sessions:\n\n";
  for (const auto& session : sessions) {
    std::cout << "\t" << session.name << ": " << session.service_path
              << std::endl;
  }

  // To get a PuppetMaster service for a session, use the following code:
  // auto puppet_master = ConnectToPuppetMaster(sessions[i]);

  return 0;
}
