// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/story_command_executor.h"

#include <lib/fxl/functional/make_copyable.h>

namespace modular {

namespace {
std::vector<fuchsia::modular::StoryCommand> CloneCommands(
    const std::vector<fuchsia::modular::StoryCommand>& commands) {
  std::vector<fuchsia::modular::StoryCommand> commands_copy;
  for (const auto& command : commands) {
    fuchsia::modular::StoryCommand clone;
    command.Clone(&clone);
    commands_copy.push_back(std::move(clone));
  }

  return commands_copy;
}
}  // namespace

StoryCommandExecutor::StoryCommandExecutor() : weak_factory_(this) {}
StoryCommandExecutor::~StoryCommandExecutor() = default;

void StoryCommandExecutor::ExecuteCommands(
    fidl::StringPtr story_id,
    std::vector<fuchsia::modular::StoryCommand> commands,
    std::function<void(fuchsia::modular::ExecuteResult)> done) {
  // TODO(thatguy): Cloning the commands here is unforunate. We will want to
  // create a shared datastructure at some point, such as a
  // ExecuteCommandsContext struct that contains the commands, and also allows
  // command runners to make modifications to the commands so that observers get
  // commands that are more fully specified.
  //
  // For now, we make a copy.
  auto commands_copy = CloneCommands(commands);
  auto on_execute_done =
      [weak_this = weak_factory_.GetWeakPtr(),
       commands_copy = std::move(commands_copy),
       done = std::move(done)](fuchsia::modular::ExecuteResult result) {
        if (!weak_this)
          return;

        for (auto& listener : weak_this->listeners_) {
          listener(commands_copy, result);
        }

        done(result);
      };

  ExecuteCommandsInternal(std::move(story_id), std::move(commands),
                          fxl::MakeCopyable(std::move(on_execute_done)));
}

StoryCommandExecutor::ListenerAutoCancel StoryCommandExecutor::AddListener(
    ListenerCallback listener) {
  auto it = listeners_.insert(listeners_.end(), std::move(listener));

  auto auto_remove = [weak_this = weak_factory_.GetWeakPtr(), it] {
    if (!weak_this)
      return;

    weak_this->listeners_.erase(it);
  };
  return ListenerAutoCancel(std::move(auto_remove));
}

}  // namespace modular
