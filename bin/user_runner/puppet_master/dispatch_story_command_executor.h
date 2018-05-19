// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fidl/cpp/string.h"
#include "peridot/bin/user_runner/puppet_master/story_command_executor.h"

namespace modular {

class OperationContainer;

// An implementation of StoryCommandExecutor which dispatches execution of
// individual StoryCommands to CommandRunners for each union tag of
// StoryCommand.
class DispatchStoryCommandExecutor : public StoryCommandExecutor {
 public:
  class CommandRunner;
  // Returns an OperationContainer* for a given |story_id|, or nullptr if
  // |story_id| is invalid. OperationContainer instances must outlive the time
  // between a call to ExecuteCommands(story_id, ...) until its |done| callback
  // is invoked.
  using OperationContainerAccessor =
      std::function<OperationContainer*(const fidl::StringPtr& story_id)>;

  DispatchStoryCommandExecutor(
      OperationContainerAccessor container_accessor,
      std::map<StoryCommand::Tag, std::unique_ptr<CommandRunner>>
          command_runners);
  ~DispatchStoryCommandExecutor() override;

 private:
  // |StoryCommandExecutor|
  void ExecuteCommands(fidl::StringPtr story_id,
                       std::vector<StoryCommand> commands,
                       std::function<void(ExecuteResult)> done) override;

  class ExecuteStoryCommandsCall;

  OperationContainerAccessor container_accessor_;
  const std::map<StoryCommand::Tag, std::unique_ptr<CommandRunner>>
      command_runners_;

  // Lookup table from StoryCommand union tag to a human-readable string.
  const std::map<StoryCommand::Tag, const char*> story_command_tag_strings_;
};

class DispatchStoryCommandExecutor::CommandRunner {
 public:
  virtual ~CommandRunner();

  virtual void Execute(fidl::StringPtr story_id, StoryCommand command,
                       std::function<void(ExecuteResult)> done) = 0;
};

}  // namespace modular