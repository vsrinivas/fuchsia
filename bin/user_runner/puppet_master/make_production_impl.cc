// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "peridot/bin/user_runner/puppet_master/dispatch_story_command_executor.h"
#include "peridot/bin/user_runner/puppet_master/make_production_impl.h"

namespace modular {

class PuppetMasterImpl;

std::unique_ptr<StoryCommandExecutor> MakeProductionStoryCommandExecutor(
    DispatchStoryCommandExecutor::OperationContainerAccessor factory) {
  std::map<StoryCommand::Tag,
           std::unique_ptr<DispatchStoryCommandExecutor::CommandRunner>>
      command_runners;
  // TODO(thatguy): Add all required command runners.

  auto executor = std::make_unique<DispatchStoryCommandExecutor>(
      std::move(factory), std::move(command_runners));
  return executor;
}

}  // namespace modular
