// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/set_link_value_command_runner.h"

#include <lib/fxl/logging.h>

namespace modular {

SetLinkValueCommandRunner::SetLinkValueCommandRunner() {}

SetLinkValueCommandRunner::~SetLinkValueCommandRunner() = default;

void SetLinkValueCommandRunner::Execute(
    fidl::StringPtr story_id, fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_set_link_value());
  // TODO(miguelfrde): implement
}

}  // namespace modular
