// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/set_focus_state_command_runner.h"

#include <lib/fxl/logging.h>

namespace modular {

SetFocusStateCommandRunner::SetFocusStateCommandRunner(
    SessionStorage* const session_storage,
    fuchsia::modular::FocusProviderPtr focus_provider)
    : CommandRunner(session_storage),
      focus_provider_(std::move(focus_provider)) {}

SetFocusStateCommandRunner::~SetFocusStateCommandRunner() = default;

void SetFocusStateCommandRunner::Execute(
    fidl::StringPtr story_id, fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_set_focus_state());

  fuchsia::modular::ExecuteResult result;
  result.status = fuchsia::modular::ExecuteStatus::OK;

  if (command.set_focus_state().focused) {
    focus_provider_->Request(story_id);
    result.story_id = story_id;
  } else {
    // According to FIDL docs a null |story_id| brings the timeline into
    // focus, defocusing any story.
    focus_provider_->Request(nullptr);
  }

  done(std::move(result));
}

}  // namespace modular
