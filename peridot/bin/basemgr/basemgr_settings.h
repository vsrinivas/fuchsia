// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_BASEMGR_SETTINGS_H_
#define PERIDOT_BIN_BASEMGR_BASEMGR_SETTINGS_H_

#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <lib/fidl/cpp/string.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/macros.h>
#include "src/lib/files/file.h"

namespace modular {

class BasemgrSettings {
 public:
  explicit BasemgrSettings(const fxl::CommandLine& command_line);

  // Gets the usage statement for running Basemgr with command line arguments.
  static std::string GetUsage();

  // Creates a fidl table of basemgr configurations from the parsed command line
  // arguments.
  fuchsia::modular::session::BasemgrConfig CreateBasemgrConfig();

  fuchsia::modular::session::AppConfig base_shell;
  fuchsia::modular::session::AppConfig story_shell;
  fuchsia::modular::session::AppConfig sessionmgr;
  fuchsia::modular::session::AppConfig session_shell;

  std::string test_name;
  bool use_session_shell_for_story_shell_factory;
  bool disable_statistics;
  bool no_minfs;
  bool test;
  bool keep_base_shell_alive_after_login;
  bool run_base_shell_with_test_runner;
  bool enable_presenter;

 private:
  void ParseShellArgs(const std::string& value, std::vector<std::string>* args);

  // Extract the test name using knowledge of how Modular structures its
  // command lines for testing.
  static std::string FindTestName(
      const std::string& session_shell,
      const std::vector<std::string>* session_shell_args);

  FXL_DISALLOW_COPY_AND_ASSIGN(BasemgrSettings);
};

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_BASEMGR_SETTINGS_H_
