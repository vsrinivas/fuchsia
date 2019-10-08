// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/command_runners/no_op_command_runner.h"

namespace modular {

NoOpCommandRunner::NoOpCommandRunner() = default;
NoOpCommandRunner::~NoOpCommandRunner() = default;

void NoOpCommandRunner::Execute(fidl::StringPtr story_id, StoryStorage* const story_storage,
                                fuchsia::modular::StoryCommand command,
                                fit::function<void(fuchsia::modular::ExecuteResult)> done) {
  fuchsia::modular::ExecuteResult result;
  result.status = fuchsia::modular::ExecuteStatus::OK;
  done(std::move(result));
}

}  // namespace modular
