// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <map>
#include <memory>

#include "gtest/gtest.h"
#include "lib/async/cpp/task.h"
#include "lib/async/default.h"
#include "lib/fidl/cpp/string.h"
#include "lib/gtest/test_with_loop.h"
#include "peridot/bin/user_runner/puppet_master/dispatch_story_command_executor.h"
#include "peridot/public/lib/async/cpp/operation.h"

namespace fuchsia {
namespace modular {
namespace {

class TestCommandRunner : public CommandRunner {
 public:
  using ExecuteFunc =
      std::function<ExecuteStatus(fidl::StringPtr, StoryCommand)>;
  TestCommandRunner(ExecuteFunc func, bool delay_done = false)
      : func_(func), delay_done_(delay_done) {}
  ~TestCommandRunner() override = default;

  void Execute(fidl::StringPtr story_id, StoryCommand command,
               std::function<void(ExecuteResult)> done) override {
    // Post the task on the async loop to simulate a long-running task.
    async::PostTask(async_get_default(), [this, story_id,
                                          command = std::move(command),
                                          done = std::move(done)]() mutable {
      auto status = func_(story_id, std::move(command));
      ExecuteResult result;
      result.status = status;
      if (delay_done_) {
        async::PostTask(async_get_default(),
                        [result = std::move(result), done = std::move(done)]() {
                          done(std::move(result));
                        });
      } else {
        done(std::move(result));
      }
    });
  }

  ExecuteFunc func_;
  bool delay_done_;
};

class DispatchStoryCommandExecutorTest : public gtest::TestWithLoop {
 protected:
  void Reset(DispatchStoryCommandExecutor::OperationContainerAccessor
                 container_accessor) {
    executor_.reset(new DispatchStoryCommandExecutor(
        std::move(container_accessor), std::move(command_runners_)));
  }

  void AddCommandRunner(StoryCommand::Tag tag,
                        TestCommandRunner::ExecuteFunc func,
                        bool delay_done = false) {
    command_runners_.emplace(tag, new TestCommandRunner(func, delay_done));
  }

  std::unique_ptr<StoryCommandExecutor> executor_;
  std::map<StoryCommand::Tag, std::unique_ptr<CommandRunner>> command_runners_;
};

TEST_F(DispatchStoryCommandExecutorTest, InvalidStory) {
  // Returning nullptr from the OperationContainerAccessor tells us the story is
  // invalid.
  Reset([](const fidl::StringPtr& /* story_id */) { return nullptr; });

  std::vector<StoryCommand> commands;
  ExecuteResult result;
  bool got_result{false};
  executor_->ExecuteCommands("id", std::move(commands), [&](ExecuteResult r) {
    got_result = true;
    result = std::move(r);
  });

  EXPECT_TRUE(got_result);
  EXPECT_EQ(ExecuteStatus::INVALID_STORY_ID, result.status);
}

TEST_F(DispatchStoryCommandExecutorTest, Dispatching) {
  // We expect that each command is dispatched to the command runner for that
  // command.
  int actual_execute_count{0};
  std::string expected_story_id = "storyid";
  for (auto tag :
       {StoryCommand::Tag::kAddMod, StoryCommand::Tag::kRemoveMod,
        StoryCommand::Tag::kSetLinkValue, StoryCommand::Tag::kSetFocusState}) {
    AddCommandRunner(tag, [tag, &actual_execute_count, expected_story_id](
                              fidl::StringPtr story_id, StoryCommand command) {
      ++actual_execute_count;
      EXPECT_EQ(tag, command.Which());
      EXPECT_EQ(expected_story_id, story_id);
      return ExecuteStatus::OK;
    });
  }

  OperationQueue queue;
  Reset([&](const fidl::StringPtr&) { return &queue; });

  std::vector<StoryCommand> commands;
  commands.resize(4);
  commands[0].set_add_mod(AddMod());
  commands[1].set_remove_mod(RemoveMod());
  commands[2].set_set_link_value(SetLinkValue());
  commands[3].set_set_focus_state(SetFocusState());

  ExecuteResult result;
  bool got_result{false};
  executor_->ExecuteCommands(expected_story_id, std::move(commands),
                             [&](ExecuteResult r) {
                               got_result = true;
                               result = std::move(r);
                             });

  RunLoopUntilIdle();
  EXPECT_TRUE(got_result);
  EXPECT_EQ(ExecuteStatus::OK, result.status);
  EXPECT_EQ(4, actual_execute_count);
}

TEST_F(DispatchStoryCommandExecutorTest, Sequential) {
  // Commands are run sequentially.
  std::vector<std::string> names;
  // We're going to run an AddMod command first, but we'll push the "logic"
  // onto the async loop so that, if the implementation posted all of our
  // CommandRunner logic sequentially on the async loop, it would run after the
  // commands following this one. That's what |delay_done| does.
  AddCommandRunner(StoryCommand::Tag::kAddMod,
                   [&](fidl::StringPtr story_id, StoryCommand command) {
                     names.push_back(command.add_mod().mod_name->at(0));
                     return ExecuteStatus::OK;
                   },
                   true /* delay_done */);
  AddCommandRunner(StoryCommand::Tag::kRemoveMod,
                   [&](fidl::StringPtr story_id, StoryCommand command) {
                     names.push_back(command.remove_mod().mod_name->at(0));
                     return ExecuteStatus::OK;
                   });

  OperationQueue queue;
  Reset([&](const fidl::StringPtr&) { return &queue; });

  std::vector<StoryCommand> commands;
  commands.resize(2);
  AddMod add_mod;
  add_mod.mod_name.push_back("one");
  commands[0].set_add_mod(std::move(add_mod));
  RemoveMod remove_mod;
  remove_mod.mod_name.push_back("two");
  commands[1].set_remove_mod(std::move(remove_mod));

  executor_->ExecuteCommands("story_id", std::move(commands),
                             [](ExecuteResult) {});
  RunLoopUntilIdle();

  EXPECT_EQ(2u, names.size());
  EXPECT_EQ("one", names[0]);
  EXPECT_EQ("two", names[1]);
}

TEST_F(DispatchStoryCommandExecutorTest, ErrorsAbortEarly) {
  // Commands after those that report an error don't run. The reported error
  // code is returned.
  bool second_command_ran{false};
  AddCommandRunner(StoryCommand::Tag::kAddMod,
                   [](fidl::StringPtr story_id, StoryCommand command) {
                     return ExecuteStatus::INVALID_COMMAND;
                   });
  AddCommandRunner(StoryCommand::Tag::kRemoveMod,
                   [&](fidl::StringPtr story_id, StoryCommand command) {
                     second_command_ran = true;
                     return ExecuteStatus::OK;
                   });

  OperationQueue queue;
  Reset([&](const fidl::StringPtr&) { return &queue; });

  std::vector<StoryCommand> commands;
  commands.resize(2);
  commands[0].set_add_mod(AddMod());
  commands[1].set_remove_mod(RemoveMod());

  ExecuteResult result;
  executor_->ExecuteCommands("story_id", std::move(commands),
                             [&](ExecuteResult r) { result = std::move(r); });
  RunLoopUntilIdle();

  EXPECT_EQ(ExecuteStatus::INVALID_COMMAND, result.status);
  EXPECT_FALSE(second_command_ran);
}

}  // namespace
}  // namespace modular
}  // namespace fuchsia
