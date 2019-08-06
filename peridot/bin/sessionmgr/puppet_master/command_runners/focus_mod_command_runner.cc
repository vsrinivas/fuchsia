// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/command_runners/focus_mod_command_runner.h"

namespace modular {

FocusModCommandRunner::FocusModCommandRunner(
    fit::function<void(std::string, std::vector<std::string>)> module_focuser)
    : module_focuser_(std::move(module_focuser)) {}

FocusModCommandRunner::~FocusModCommandRunner() = default;

void FocusModCommandRunner::Execute(fidl::StringPtr story_id, StoryStorage* const story_storage,
                                    fuchsia::modular::StoryCommand command,
                                    fit::function<void(fuchsia::modular::ExecuteResult)> done) {
  fuchsia::modular::ExecuteResult result;

  auto focus_mod_command = command.focus_mod();

  // Prefer |mod_name_transitional| over |mod_name|
  std::vector<std::string> mod_name{};
  if (focus_mod_command.mod_name_transitional.has_value()) {
    mod_name.push_back(*focus_mod_command.mod_name_transitional);
  } else {
    mod_name = focus_mod_command.mod_name;
  }

  if (mod_name.empty()) {
    result.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
    result.error_message = "No mod_name provided.";
    done(std::move(result));
    return;
  }

  module_focuser_(story_id.value_or(""), std::move(mod_name));

  result.status = fuchsia::modular::ExecuteStatus::OK;
  done(std::move(result));
}

}  // namespace modular
