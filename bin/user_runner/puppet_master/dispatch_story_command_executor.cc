// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/dispatch_story_command_executor.h"

namespace modular {

DispatchStoryCommandExecutor::DispatchStoryCommandExecutor(
    OperationContainerFactory container_factory,
    std::map<StoryCommand::Tag, std::unique_ptr<CommandRunner>> command_runners)
    : container_factory_(std::move(container_factory)),
      command_runners_(std::move(command_runners)) {}

DispatchStoryCommandExecutor::~DispatchStoryCommandExecutor() {}

void DispatchStoryCommandExecutor::ExecuteCommands(
    fidl::StringPtr story_id, std::vector<StoryCommand> commands,
    std::function<void(ExecuteResult)> done) {}

DispatchStoryCommandExecutor::CommandRunner::~CommandRunner() = default;

}  // namespace modular
