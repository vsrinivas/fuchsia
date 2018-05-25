// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "peridot/bin/user_runner/puppet_master/dispatch_story_command_executor.h"

#include "lib/async/cpp/operation.h"
#include "lib/async/cpp/future.h"
#include "lib/fxl/functional/make_copyable.h"

namespace modular {

namespace {

class RunStoryCommandCall : public Operation<ExecuteResult> {
 public:
  RunStoryCommandCall(const char* const command_name,
                      DispatchStoryCommandExecutor::CommandRunner* const runner,
                      fidl::StringPtr story_id, StoryCommand command,
                      ResultCall done)
      : Operation(command_name, std::move(done), ""),
        story_id_(std::move(story_id)),
        command_(std::move(command)),
        runner_(runner) {}

 private:
  // |OperationBase|
  void Run() override {
    auto done = [this](ExecuteResult result) { Done(std::move(result)); };
    runner_->Execute(story_id_, std::move(command_), std::move(done));
  }

  const fidl::StringPtr story_id_;
  StoryCommand command_;
  DispatchStoryCommandExecutor::CommandRunner* runner_;
};

}  // namespace

class DispatchStoryCommandExecutor::ExecuteStoryCommandsCall
    : public Operation<ExecuteResult> {
 public:
  ExecuteStoryCommandsCall(DispatchStoryCommandExecutor* const executor,
                           fidl::StringPtr story_id,
                           std::vector<StoryCommand> commands, ResultCall done)
      : Operation("ExecuteStoryCommandsCall", std::move(done)),
        executor_(executor),
        story_id_(std::move(story_id)),
        commands_(std::move(commands)) {}

  ~ExecuteStoryCommandsCall() = default;

 private:
  void Run() override {
    // TODO(thatguy): Add a WeakPtr check on |executor_|.

    // Keep track of the number of commands we need to run. When they are all
    // done, we complete this operation.
    auto future = Future<ExecuteResult>::Create();
    std::vector<FuturePtr<ExecuteResult>> did_execute_commands;
    did_execute_commands.reserve(commands_.size());

    for (auto& command : commands_) {
      auto tag_string_it =
          executor_->story_command_tag_strings_.find(command.Which());
      FXL_CHECK(tag_string_it != executor_->story_command_tag_strings_.end())
          << "No StoryCommand::Tag string for tag "
          << static_cast<int>(command.Which());
      const auto& tag_string = tag_string_it->second;

      auto it = executor_->command_runners_.find(command.Which());
      FXL_DCHECK(it != executor_->command_runners_.end())
          << "Could not find a StoryCommand runner for tag "
          << static_cast<int>(command.Which()) << ": " << tag_string;

      auto* const command_runner = it->second.get();
      // NOTE: it is safe to capture |this| on the lambdas below because if
      // |this| goes out of scope, |queue_| will be deleted, and the callbacks
      // on |queue_| will not run.

      auto did_execute_command = Future<ExecuteResult>::Create();
      queue_.Add(new RunStoryCommandCall(
          tag_string, command_runner, story_id_, std::move(command),
          did_execute_command->Completer()));
      did_execute_command->Then(
          [this](ExecuteResult result) {
            // Check for error for this command. If there was an error, abort
            // early. All of the remaining operations (if any) in queue_ will
            // not be run.
            if (result.status != ExecuteStatus::OK) {
              Done(std::move(result));
            }
          });
      did_execute_commands.emplace_back(did_execute_command);
    }

    Future<ExecuteResult>::Wait(did_execute_commands)->Then([this] {
      ExecuteResult result;
      result.status = ExecuteStatus::OK;
      result.story_id = story_id_;
      Done(std::move(result));
    });
  }

  DispatchStoryCommandExecutor* const executor_;
  const fidl::StringPtr story_id_;
  std::vector<StoryCommand> commands_;

  // All commands must be run in order so we use a queue.
  OperationQueue queue_;
};

DispatchStoryCommandExecutor::DispatchStoryCommandExecutor(
    OperationContainerAccessor container_accessor,
    std::map<StoryCommand::Tag, std::unique_ptr<CommandRunner>> command_runners)
    : container_accessor_(std::move(container_accessor)),
      command_runners_(std::move(command_runners)),
      story_command_tag_strings_{
          {StoryCommand::Tag::kAddMod, "StoryCommand::AddMod"},
          {StoryCommand::Tag::kRemoveMod, "StoryCommand::RemoveMod"},
          {StoryCommand::Tag::kSetLinkValue, "StoryCommand::SetLinkValue"},
          {StoryCommand::Tag::kSetFocusState, "StoryCommand::SetFocusState"}} {}

DispatchStoryCommandExecutor::~DispatchStoryCommandExecutor() {}

void DispatchStoryCommandExecutor::ExecuteCommands(
    fidl::StringPtr story_id, std::vector<StoryCommand> commands,
    std::function<void(ExecuteResult)> done) {
  OperationContainer* const container = container_accessor_(story_id);
  if (!container) {
    ExecuteResult result;
    result.status = ExecuteStatus::INVALID_STORY_ID;
    done(result);
    return;
  }

  container->Add(new ExecuteStoryCommandsCall(
      this, std::move(story_id), std::move(commands), std::move(done)));
}

DispatchStoryCommandExecutor::CommandRunner::~CommandRunner() = default;

}  // namespace modular
