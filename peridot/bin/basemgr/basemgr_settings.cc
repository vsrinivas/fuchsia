// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/basemgr_settings.h"

#include <lib/fidl/cpp/string.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/macros.h>

#include <string>

#include "peridot/lib/modular_config/modular_config_constants.h"
#include "src/lib/files/file.h"

namespace modular {

BasemgrSettings::BasemgrSettings(const fxl::CommandLine& command_line) {
  base_shell.set_url(command_line.GetOptionValueWithDefault(
      modular_config::kBaseShell, modular_config::kDefaultBaseShellUrl));
  story_shell.set_url(command_line.GetOptionValueWithDefault(
      modular_config::kStoryShell, modular_config::kDefaultStoryShellUrl));
  sessionmgr.set_url(command_line.GetOptionValueWithDefault(
      modular_config::kSessionmgrConfigName, modular_config::kSessionmgrUrl));
  session_shell.set_url(command_line.GetOptionValueWithDefault(
      modular_config::kSessionShell, modular_config::kDefaultSessionShellUrl));

  use_session_shell_for_story_shell_factory = command_line.HasOption(
      modular_config::kUseSessionShellForStoryShellFactory);

  disable_statistics =
      command_line.HasOption(modular_config::kDisableStatistics);
  no_minfs = command_line.HasOption(modular_config::kNoMinfs);
  test = command_line.HasOption(modular_config::kTest);

  // This flag will be exposed with the completion of MF-10. For now, we will
  // set it based on the test flag.
  // Current integration tests expect base shell to always be running, therefore
  // we will keep the base shell alive after login in all test cases.
  keep_base_shell_alive_after_login = test;

  run_base_shell_with_test_runner =
      command_line.GetOptionValueWithDefault(
          modular_config::kRunBaseShellWithTestRunner, modular_config::kTrue) ==
              modular_config::kTrue
          ? true
          : false;
  enable_presenter = command_line.HasOption(modular_config::kEnablePresenter);

  ParseShellArgs(command_line.GetOptionValueWithDefault(
                     modular_config::kBaseShellArgs, ""),
                 base_shell.mutable_args());

  ParseShellArgs(command_line.GetOptionValueWithDefault(
                     modular_config::kStoryShellArgs, ""),
                 story_shell.mutable_args());

  ParseShellArgs(command_line.GetOptionValueWithDefault(
                     modular_config::kSessionmgrArgs, ""),
                 sessionmgr.mutable_args());

  ParseShellArgs(command_line.GetOptionValueWithDefault(
                     modular_config::kSessionShellArgs, ""),
                 session_shell.mutable_args());

  if (test) {
    if (run_base_shell_with_test_runner) {
      base_shell.mutable_args()->push_back("--use_test_runner");
    }
    sessionmgr.mutable_args()->push_back("--enable_story_shell_preload=false");
    sessionmgr.mutable_args()->push_back("--enable_cobalt=false");

    // Sessionmgr will always read product configurations and override configs
    // that are passed as flags. Until we transition our tests to use the
    // modular config system, we will use the flag use_default_config to
    // indicate to sessionmgr that we are running in a test scenario and that we
    // need default sessionmgr configurations.
    sessionmgr.mutable_args()->push_back("--use_default_config");
    test_name = FindTestName(session_shell.url(), &session_shell.args());
    disable_statistics = true;
    no_minfs = true;
  }
}

// Temporary way to transform commandline args into FIDL table
fuchsia::modular::session::BasemgrConfig
BasemgrSettings::CreateBasemgrConfig() {
  fuchsia::modular::session::BasemgrConfig config;

  config.set_enable_cobalt(!disable_statistics);
  config.set_enable_presenter(enable_presenter);
  config.set_use_minfs(!no_minfs);
  config.set_use_session_shell_for_story_shell_factory(
      use_session_shell_for_story_shell_factory);
  config.set_test(test);
  config.set_test_name(test_name);

  config.mutable_base_shell()->set_app_config(std::move(base_shell));
  config.mutable_base_shell()->set_keep_alive_after_login(
      keep_base_shell_alive_after_login);

  fuchsia::modular::session::SessionShellMapEntry session_shell_entry;
  session_shell_entry.set_name(session_shell.url());
  session_shell_entry.mutable_config()->set_app_config(
      std::move(session_shell));
  // Set default presenter settings
  session_shell_entry.mutable_config()->set_display_usage(
      fuchsia::ui::policy::DisplayUsage::kUnknown);
  session_shell_entry.mutable_config()->set_screen_height(
      std::numeric_limits<float>::signaling_NaN());
  session_shell_entry.mutable_config()->set_screen_width(
      std::numeric_limits<float>::signaling_NaN());
  config.mutable_session_shell_map()->push_back(std::move(session_shell_entry));

  config.mutable_story_shell()->set_app_config(std::move(story_shell));
  config.set_sessionmgr(std::move(sessionmgr));

  return config;
}

std::string BasemgrSettings::GetUsage() {
  return R"USAGE(basemgr
      --base_shell=BASE_SHELL
      --base_shell_args=SHELL_ARGS
      --session_shell=SESSION_SHELL
      --session_shell_args=SHELL_ARGS
      --story_shell=STORY_SHELL
      --story_shell_args=SHELL_ARGS
      --use_session_shell_for_story_shell_factory
      --disable_statistics
      --no_minfs
      --test
      --enable_presenter
    DEVICE_NAME: Name which session shell uses to identify this device.
    BASE_SHELL:  URL of the base shell to run.
                Defaults to "dev_base_shell".
                For integration testing use "dev_base_shell".
    SESSIONMGR: URL of the sessionmgr to run.
                Defaults to "sessionmgr".
    SESSION_SHELL: URL of the session shell to run.
                Defaults to "ermine_session_shell".
                For integration testing use "dev_session_shell".
    STORY_SHELL: URL of the story shell to run.
                Defaults to "mondrian".
                For integration testing use "dev_story_shell".
    SHELL_ARGS: Comma separated list of arguments. Backslash escapes comma.
    --use_session_shell_for_story_shell_factory:
                Create story shells through StoryShellFactory exposed
                by the session shell instead of creating separate story shell
                components. When set, the --story_shell and --story_shell_args
                flags are ignored.)USAGE";
}

void BasemgrSettings::ParseShellArgs(const std::string& value,
                                     std::vector<std::string>* args) {
  bool escape = false;
  std::string arg;
  for (char i : value) {
    if (escape) {
      arg.push_back(i);
      escape = false;
      continue;
    }

    if (i == '\\') {
      escape = true;
      continue;
    }

    if (i == ',') {
      args->push_back(arg);
      arg.clear();
      continue;
    }

    arg.push_back(i);
  }

  if (!arg.empty()) {
    args->push_back(arg);
  }
}

std::string BasemgrSettings::FindTestName(
    const std::string& session_shell,
    const std::vector<std::string>* session_shell_args) {
  const std::string kRootModule = "--root_module";
  std::string result = session_shell;

  for (const auto& arg : *session_shell_args) {
    if (arg.substr(0, kRootModule.size()) == kRootModule) {
      result = arg.substr(kRootModule.size());
    }
  }

  const auto index = result.find_last_of('/');
  if (index == std::string::npos) {
    return result;
  }

  return result.substr(index + 1);
}

}  // namespace modular
