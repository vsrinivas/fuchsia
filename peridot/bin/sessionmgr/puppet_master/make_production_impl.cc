// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/make_production_impl.h"

#include <memory>

#include "peridot/bin/sessionmgr/puppet_master/command_runners/add_mod_command_runner.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/focus_mod_command_runner.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/remove_mod_command_runner.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/set_focus_state_command_runner.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/set_kind_of_proto_story_option_command_runner.h"
#include "peridot/bin/sessionmgr/puppet_master/command_runners/set_link_value_command_runner.h"
#include "peridot/bin/sessionmgr/puppet_master/dispatch_story_command_executor.h"

namespace modular {

class PuppetMasterImpl;

using StoryControllerFactory =
    fit::function<fuchsia::modular::StoryControllerPtr(fidl::StringPtr story_id)>;

std::unique_ptr<StoryCommandExecutor> MakeProductionStoryCommandExecutor(
    SessionStorage* const session_storage, fuchsia::modular::FocusProviderPtr focus_provider,
    fuchsia::modular::ModuleResolver* const module_resolver,
    fuchsia::modular::EntityResolver* const entity_resolver,
    // TODO(miguelfrde): we shouldn't create this dependency here. Instead
    // an interface similar to StoryStorage should be created for Runtime
    // use cases.
    fit::function<void(std::string, std::vector<std::string>)> module_focuser) {
  std::map<fuchsia::modular::StoryCommand::Tag, std::unique_ptr<CommandRunner>> command_runners;
  command_runners.emplace(fuchsia::modular::StoryCommand::Tag::kSetFocusState,
                          new SetFocusStateCommandRunner(std::move(focus_provider)));
  command_runners.emplace(fuchsia::modular::StoryCommand::Tag::kAddMod,
                          new AddModCommandRunner(module_resolver, entity_resolver));
  command_runners.emplace(fuchsia::modular::StoryCommand::Tag::kFocusMod,
                          new FocusModCommandRunner(std::move(module_focuser)));
  command_runners.emplace(fuchsia::modular::StoryCommand::Tag::kRemoveMod,
                          new RemoveModCommandRunner());
  command_runners.emplace(fuchsia::modular::StoryCommand::Tag::kSetLinkValue,
                          new SetLinkValueCommandRunner());
  command_runners.emplace(fuchsia::modular::StoryCommand::Tag::kSetKindOfProtoStoryOption,
                          new SetKindOfProtoStoryOptionCommandRunner(session_storage));

  auto executor =
      std::make_unique<DispatchStoryCommandExecutor>(session_storage, std::move(command_runners));
  return executor;
}

}  // namespace modular
