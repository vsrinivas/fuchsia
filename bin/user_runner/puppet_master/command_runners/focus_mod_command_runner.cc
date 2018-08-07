// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/focus_mod_command_runner.h"

namespace modular {

FocusModCommandRunner::FocusModCommandRunner(
    fit::function<void(fidl::StringPtr, fidl::VectorPtr<fidl::StringPtr>)>
        module_focuser)
    : module_focuser_(std::move(module_focuser)) {}

FocusModCommandRunner::~FocusModCommandRunner() = default;

void FocusModCommandRunner::Execute(
    fidl::StringPtr story_id, StoryStorage* const story_storage,
    fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  fuchsia::modular::ExecuteResult result;

  if (command.focus_mod().mod_name->empty()) {
    result.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
    result.error_message = "No mod_name provided.";
    done(std::move(result));
    return;
  }

  module_focuser_(story_id, std::move(command.focus_mod().mod_name));

  result.status = fuchsia::modular::ExecuteStatus::OK;
  done(std::move(result));
}

}  // namespace modular
