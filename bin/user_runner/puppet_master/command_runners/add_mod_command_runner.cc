// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/add_mod_command_runner.h"

#include <lib/fxl/logging.h>

namespace modular {

AddModCommandRunner::AddModCommandRunner() {}

AddModCommandRunner::~AddModCommandRunner() = default;

void AddModCommandRunner::Execute(
    fidl::StringPtr story_id, fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_add_mod());
}

}  // namespace modular
