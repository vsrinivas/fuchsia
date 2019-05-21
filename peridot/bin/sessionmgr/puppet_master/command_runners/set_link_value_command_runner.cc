// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/command_runners/set_link_value_command_runner.h"
#include <lib/fsl/vmo/strings.h>

#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/set_link_value_call.h"

namespace modular {

SetLinkValueCommandRunner::SetLinkValueCommandRunner() = default;
SetLinkValueCommandRunner::~SetLinkValueCommandRunner() = default;

void SetLinkValueCommandRunner::Execute(
    fidl::StringPtr story_id, StoryStorage* const story_storage,
    fuchsia::modular::StoryCommand command,
    fit::function<void(fuchsia::modular::ExecuteResult)> done) {
  FXL_CHECK(command.is_set_link_value());

  AddSetLinkValueOperation(
      &operation_queue_, story_storage,
      std::move(command.set_link_value().path),
      [new_value =
           std::move(command.set_link_value().value)](fidl::StringPtr* value) {
        std::string str_value;
        FXL_CHECK(fsl::StringFromVmo(*new_value, &str_value));
        *value = str_value;
      },
      std::move(done));
}

}  // namespace modular
