// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fidl/cpp/string.h"
#include "peridot/bin/user_runner/puppet_master/story_command_executor.h"

namespace modular {

class OperationContainer;

class DispatchStoryCommandExecutor : public StoryCommandExecutor {
 public:
  class CommandRunner;
  // Returns an OperationContainer* for a given |story_id|, or nullptr if
  // |story_id| is invalid.
  using OperationContainerFactory =
      std::function<OperationContainer*(const fidl::StringPtr& story_id)>;

  DispatchStoryCommandExecutor(
      OperationContainerFactory container_factory,
      std::map<StoryCommand::Tag, std::unique_ptr<CommandRunner>>
          command_runners);
  ~DispatchStoryCommandExecutor() override;

 private:
  void ExecuteCommands(fidl::StringPtr story_id,
                       std::vector<StoryCommand> commands,
                       std::function<void(ExecuteResult)> done) override;

  OperationContainerFactory container_factory_;
  const std::map<StoryCommand::Tag, std::unique_ptr<CommandRunner>>
      command_runners_;
};

class DispatchStoryCommandExecutor::CommandRunner {
 public:
  virtual ~CommandRunner();

  virtual void Execute(
      fidl::StringPtr story_id, StoryCommand command,
      std::function<void(ExecuteStatus, std::string)> done) = 0;
};

}  // namespace modular