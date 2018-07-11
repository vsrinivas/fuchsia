// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/set_focus_state_command_runner.h"

#include <lib/fxl/logging.h>

namespace modular {

SetFocusStateCommandRunner::SetFocusStateCommandRunner(
    SessionStorage* const session_storage)
    : CommandRunner(session_storage) {}

SetFocusStateCommandRunner::~SetFocusStateCommandRunner() = default;

void SetFocusStateCommandRunner::Execute(
    fidl::StringPtr story_id, fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_set_focus_state());
}

}  // namespace modular
