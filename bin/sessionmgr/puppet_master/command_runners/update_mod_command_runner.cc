// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/command_runners/update_mod_command_runner.h"

#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/update_mod_call.h"

namespace modular {

UpdateModCommandRunner::UpdateModCommandRunner() = default;
UpdateModCommandRunner::~UpdateModCommandRunner() = default;

void UpdateModCommandRunner::Execute(
    fidl::StringPtr story_id, StoryStorage* const story_storage,
    fuchsia::modular::StoryCommand command,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_update_mod());

  AddUpdateModOperation(&operation_queue_, story_storage,
                        std::move(command.update_mod()), std::move(done));
}

}  // namespace modular
