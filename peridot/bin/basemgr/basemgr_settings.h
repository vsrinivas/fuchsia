// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_BASEMGR_SETTINGS_H_
#define PERIDOT_BIN_BASEMGR_BASEMGR_SETTINGS_H_

#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/macros.h>
#include "src/lib/files/file.h"

namespace modular {

class BasemgrSettings {
 public:
  explicit BasemgrSettings(const fxl::CommandLine& command_line);

  static std::string GetUsage();

  fuchsia::modular::AppConfig base_shell;
  fuchsia::modular::AppConfig story_shell;
  fuchsia::modular::AppConfig sessionmgr;
  fuchsia::modular::AppConfig session_shell;

  std::string test_name;
  bool use_session_shell_for_story_shell_factory;
  bool disable_statistics;
  bool no_minfs;
  bool test;
  bool run_base_shell_with_test_runner;
  bool enable_presenter;

 private:
  void ParseShellArgs(const std::string& value,
                      fidl::VectorPtr<std::string>* args);

  // Extract the test name using knowledge of how Modular structures its
  // command lines for testing.
  static std::string FindTestName(
      const fidl::StringPtr& session_shell,
      const fidl::VectorPtr<std::string>& session_shell_args);

  FXL_DISALLOW_COPY_AND_ASSIGN(BasemgrSettings);
};

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_BASEMGR_SETTINGS_H_
