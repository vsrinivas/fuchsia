// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/make_production_impl.h"

#include <memory>

#include "peridot/bin/user_runner/puppet_master/command_runners/set_link_value_command_runner.h"
#include "peridot/bin/user_runner/puppet_master/command_runners/update_mod_command_runner.h"
#include "peridot/bin/user_runner/puppet_master/dispatch_story_command_executor.h"

namespace modular {

class PuppetMasterImpl;

std::unique_ptr<StoryCommandExecutor> MakeProductionStoryCommandExecutor(
    DispatchStoryCommandExecutor::OperationContainerAccessor factory) {
  std::map<fuchsia::modular::StoryCommand::Tag, std::unique_ptr<CommandRunner>> command_runners;
  // TODO(thatguy): Add all required command runners.
  command_runners.emplace(fuchsia::modular::StoryCommand::Tag::kSetLinkValue,
                          new SetLinkValueCommandRunner());
  command_runners.emplace(fuchsia::modular::StoryCommand::Tag::kUpdateMod,
                          new UpdateModCommandRunner());

  auto executor = std::make_unique<DispatchStoryCommandExecutor>(
      std::move(factory), std::move(command_runners));
  return executor;
}

}  // namespace modular
