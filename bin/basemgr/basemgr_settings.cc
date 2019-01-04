// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/basemgr_settings.h"

#include <string>

#include <lib/fidl/cpp/string.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/macros.h>

namespace modular {

BasemgrSettings::BasemgrSettings(const fxl::CommandLine& command_line) {
  base_shell.url = command_line.GetOptionValueWithDefault(
      "base_shell",
      "fuchsia-pkg://fuchsia.com/userpicker_base_shell#meta/"
      "userpicker_base_shell.cmx");
  story_shell.url =
      command_line.GetOptionValueWithDefault("story_shell", "mondrian");
  sessionmgr.url = command_line.GetOptionValueWithDefault(
      "sessionmgr", "fuchsia-pkg://fuchsia.com/sessionmgr#meta/sessionmgr.cmx");
  session_shell.url = command_line.GetOptionValueWithDefault(
      "session_shell", "ermine_session_shell");
  account_provider.url = command_line.GetOptionValueWithDefault(
      "account_provider", "token_manager_factory");

  disable_statistics = command_line.HasOption("disable_statistics");
  no_minfs = command_line.HasOption("no_minfs");
  test = command_line.HasOption("test");
  run_base_shell_with_test_runner =
      command_line.GetOptionValueWithDefault("run_base_shell_with_test_runner",
                                             "true") == "true"
          ? true
          : false;
  enable_presenter = command_line.HasOption("enable_presenter");

  ParseShellArgs(command_line.GetOptionValueWithDefault("base_shell_args", ""),
                 &base_shell.args);

  ParseShellArgs(command_line.GetOptionValueWithDefault("story_shell_args", ""),
                 &story_shell.args);

  ParseShellArgs(command_line.GetOptionValueWithDefault("sessionmgr_args", ""),
                 &sessionmgr.args);

  ParseShellArgs(
      command_line.GetOptionValueWithDefault("session_shell_args", ""),
      &session_shell.args);

  if (test) {
    if (run_base_shell_with_test_runner) {
      base_shell.args.push_back("--test");
    }
    story_shell.args.push_back("--test");
    sessionmgr.args.push_back("--test");
    session_shell.args.push_back("--test");
    test_name = FindTestName(session_shell.url, session_shell.args);
    disable_statistics = true;
    no_minfs = true;
  }
}

std::string BasemgrSettings::GetUsage() {
  return R"USAGE(basemgr
      --base_shell=BASE_SHELL
      --base_shell_args=SHELL_ARGS
      --session_shell=SESSION_SHELL
      --session_shell_args=SHELL_ARGS
      --story_shell=STORY_SHELL
      --story_shell_args=SHELL_ARGS
      --account_provider=ACCOUNT_PROVIDER
      --disable_statistics
      --no_minfs
      --test
      --enable_presenter
    DEVICE_NAME: Name which session shell uses to identify this device.
    BASE_SHELL:  URL of the base shell to run.
                Defaults to "userpicker_base_shell".
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
    ACCOUNT_PROVIDER: URL of the account provider to use.
                Defaults to "oauth_token_manager".
                For integration tests use "dev_token_manager".)USAGE";
}

void BasemgrSettings::ParseShellArgs(const std::string& value,
                                     fidl::VectorPtr<std::string>* args) {
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
    const fidl::StringPtr& session_shell,
    const fidl::VectorPtr<std::string>& session_shell_args) {
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
