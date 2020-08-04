// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/make_production_impl.h"

#include <memory>

#include "src/modular/bin/sessionmgr/puppet_master/command_runners/add_mod_command_runner.h"
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/remove_mod_command_runner.h"
#include "src/modular/bin/sessionmgr/puppet_master/dispatch_story_command_executor.h"

namespace modular {

class PuppetMasterImpl;

using StoryControllerFactory =
    fit::function<fuchsia::modular::StoryControllerPtr(fidl::StringPtr story_id)>;

std::unique_ptr<StoryCommandExecutor> MakeProductionStoryCommandExecutor(
    SessionStorage* const session_storage) {
  std::map<fuchsia::modular::StoryCommand::Tag, std::unique_ptr<CommandRunner>> command_runners;
  command_runners.emplace(fuchsia::modular::StoryCommand::Tag::kAddMod, new AddModCommandRunner());
  command_runners.emplace(fuchsia::modular::StoryCommand::Tag::kRemoveMod,
                          new RemoveModCommandRunner());

  auto executor =
      std::make_unique<DispatchStoryCommandExecutor>(session_storage, std::move(command_runners));
  return executor;
}

}  // namespace modular
