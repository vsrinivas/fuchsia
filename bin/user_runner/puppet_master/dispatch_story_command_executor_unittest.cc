// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/dispatch_story_command_executor.h"

#include <map>
#include <memory>

#include <fuchsia/modular/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/async/cpp/task.h"
#include "lib/async/default.h"
#include "lib/fidl/cpp/string.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/public/lib/async/cpp/operation.h"

namespace modular {
namespace {

class TestCommandRunner : public CommandRunner {
 public:
  using ExecuteFunc = std::function<fuchsia::modular::ExecuteStatus(
      fidl::StringPtr, fuchsia::modular::StoryCommand)>;
  TestCommandRunner(ExecuteFunc func, bool delay_done = false)
      : func_(func), delay_done_(delay_done) {}
  ~TestCommandRunner() override = default;

  void Execute(
      fidl::StringPtr story_id, fuchsia::modular::StoryCommand command,
      std::function<void(fuchsia::modular::ExecuteResult)> done) override {
    // Post the task on the async loop to simulate a long-running task.
    async::PostTask(async_get_default(), [this, story_id,
                                          command = std::move(command),
                                          done = std::move(done)]() mutable {
      auto status = func_(story_id, std::move(command));
      fuchsia::modular::ExecuteResult result;
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

class DispatchStoryCommandExecutorTest : public gtest::TestLoopFixture {
 protected:
  void Reset(DispatchStoryCommandExecutor::OperationContainerAccessor
                 container_accessor) {
    executor_.reset(new DispatchStoryCommandExecutor(
        std::move(container_accessor), std::move(command_runners_)));
  }

  void AddCommandRunner(fuchsia::modular::StoryCommand::Tag tag,
                        TestCommandRunner::ExecuteFunc func,
                        bool delay_done = false) {
    command_runners_.emplace(tag, new TestCommandRunner(func, delay_done));
  }

  std::unique_ptr<StoryCommandExecutor> executor_;
  std::map<fuchsia::modular::StoryCommand::Tag, std::unique_ptr<CommandRunner>> command_runners_;
};

TEST_F(DispatchStoryCommandExecutorTest, InvalidStory) {
  // Returning nullptr from the OperationContainerAccessor tells us the story is
  // invalid.
  Reset([](const fidl::StringPtr& /* story_id */) { return nullptr; });

  std::vector<fuchsia::modular::StoryCommand> commands;
  fuchsia::modular::ExecuteResult result;
  bool got_result{false};
  executor_->ExecuteCommands("id", std::move(commands),
                             [&](fuchsia::modular::ExecuteResult r) {
                               got_result = true;
                               result = std::move(r);
                             });

  EXPECT_TRUE(got_result);
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_STORY_ID, result.status);
}

TEST_F(DispatchStoryCommandExecutorTest, Dispatching) {
  // We expect that each command is dispatched to the command runner for that
  // command.
  int actual_execute_count{0};
  std::string expected_story_id = "storyid";
  for (auto tag : {fuchsia::modular::StoryCommand::Tag::kAddMod,
                   fuchsia::modular::StoryCommand::Tag::kRemoveMod,
                   fuchsia::modular::StoryCommand::Tag::kSetLinkValue,
                   fuchsia::modular::StoryCommand::Tag::kSetFocusState}) {
    AddCommandRunner(tag, [tag, &actual_execute_count, expected_story_id](
                              fidl::StringPtr story_id,
                              fuchsia::modular::StoryCommand command) {
      ++actual_execute_count;
      EXPECT_EQ(tag, command.Which());
      EXPECT_EQ(expected_story_id, story_id);
      return fuchsia::modular::ExecuteStatus::OK;
    });
  }

  OperationQueue queue;
  Reset([&](const fidl::StringPtr&) { return &queue; });

  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.resize(4);
  commands[0].set_add_mod(fuchsia::modular::AddMod());
  commands[1].set_remove_mod(fuchsia::modular::RemoveMod());
  commands[2].set_set_link_value(fuchsia::modular::SetLinkValue());
  commands[3].set_set_focus_state(fuchsia::modular::SetFocusState());

  fuchsia::modular::ExecuteResult result;
  bool got_result{false};
  executor_->ExecuteCommands(expected_story_id, std::move(commands),
                             [&](fuchsia::modular::ExecuteResult r) {
                               got_result = true;
                               result = std::move(r);
                             });

  RunLoopUntilIdle();
  EXPECT_TRUE(got_result);
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
  EXPECT_EQ(4, actual_execute_count);
}

TEST_F(DispatchStoryCommandExecutorTest, Sequential) {
  // Commands are run sequentially.
  std::vector<std::string> names;
  // We're going to run an fuchsia::modular::AddMod command first, but we'll
  // push the "logic" onto the async loop so that, if the implementation posted
  // all of our CommandRunner logic sequentially on the async loop, it would run
  // after the commands following this one. That's what |delay_done| does.
  AddCommandRunner(
      fuchsia::modular::StoryCommand::Tag::kAddMod,
      [&](fidl::StringPtr story_id, fuchsia::modular::StoryCommand command) {
        names.push_back(command.add_mod().mod_name->at(0));
        return fuchsia::modular::ExecuteStatus::OK;
      },
      true /* delay_done */);
  AddCommandRunner(
      fuchsia::modular::StoryCommand::Tag::kRemoveMod,
      [&](fidl::StringPtr story_id, fuchsia::modular::StoryCommand command) {
        names.push_back(command.remove_mod().mod_name->at(0));
        return fuchsia::modular::ExecuteStatus::OK;
      });

  OperationQueue queue;
  Reset([&](const fidl::StringPtr&) { return &queue; });

  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.resize(2);
  fuchsia::modular::AddMod add_mod;
  add_mod.mod_name.push_back("one");
  commands[0].set_add_mod(std::move(add_mod));
  fuchsia::modular::RemoveMod remove_mod;
  remove_mod.mod_name.push_back("two");
  commands[1].set_remove_mod(std::move(remove_mod));

  executor_->ExecuteCommands("story_id", std::move(commands),
                             [](fuchsia::modular::ExecuteResult) {});
  RunLoopUntilIdle();

  EXPECT_EQ(2u, names.size());
  EXPECT_EQ("one", names[0]);
  EXPECT_EQ("two", names[1]);
}

TEST_F(DispatchStoryCommandExecutorTest, ErrorsAbortEarly) {
  // Commands after those that report an error don't run. The reported error
  // code is returned.
  bool second_command_ran{false};
  AddCommandRunner(
      fuchsia::modular::StoryCommand::Tag::kAddMod,
      [](fidl::StringPtr story_id, fuchsia::modular::StoryCommand command) {
        return fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
      });
  AddCommandRunner(
      fuchsia::modular::StoryCommand::Tag::kRemoveMod,
      [&](fidl::StringPtr story_id, fuchsia::modular::StoryCommand command) {
        second_command_ran = true;
        return fuchsia::modular::ExecuteStatus::OK;
      });

  OperationQueue queue;
  Reset([&](const fidl::StringPtr&) { return &queue; });

  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.resize(2);
  commands[0].set_add_mod(fuchsia::modular::AddMod());
  commands[1].set_remove_mod(fuchsia::modular::RemoveMod());

  fuchsia::modular::ExecuteResult result;
  executor_->ExecuteCommands(
      "story_id", std::move(commands),
      [&](fuchsia::modular::ExecuteResult r) { result = std::move(r); });
  RunLoopUntilIdle();

  EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_COMMAND, result.status);
  EXPECT_FALSE(second_command_ran);
}

}  // namespace
}  // namespace modular
