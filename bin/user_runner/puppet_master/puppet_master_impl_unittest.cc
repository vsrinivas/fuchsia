// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/puppet_master_impl.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/gtest/test_with_loop.h"
#include "peridot/bin/user_runner/puppet_master/story_command_executor.h"

namespace modular {
namespace {

class TestStoryCommandExecutor : public StoryCommandExecutor {
 public:
  void SetExecuteReturnResult(fuchsia::modular::ExecuteStatus status,
                              fidl::StringPtr error_message,
                              fidl::StringPtr story_id) {
    result_.status = status;
    result_.error_message = error_message;
    result_.story_id = story_id;
  }

  int execute_count{0};
  fidl::StringPtr last_story_id;
  std::vector<fuchsia::modular::StoryCommand> last_commands;

 private:
  // |StoryCommandExecutor|
  void ExecuteCommands(
      fidl::StringPtr story_id,
      std::vector<fuchsia::modular::StoryCommand> commands,
      std::function<void(fuchsia::modular::ExecuteResult)> done) override {
    ++execute_count;
    last_story_id = story_id;
    last_commands = std::move(commands);
    fuchsia::modular::ExecuteResult result;
    fidl::Clone(result_, &result);
    done(std::move(result));
  }

  fuchsia::modular::ExecuteResult result_;
};

fuchsia::modular::StoryCommand MakeRemoveModCommand(std::string mod_name) {
  fuchsia::modular::StoryCommand command;
  fuchsia::modular::RemoveMod remove_mod;
  remove_mod.mod_name.push_back(mod_name);
  command.set_remove_mod(std::move(remove_mod));
  return command;
}

class PuppetMasterTest : public gtest::TestWithLoop {
 public:
  PuppetMasterTest() : impl_(&executor_) { impl_.Connect(ptr_.NewRequest()); }

  fuchsia::modular::StoryPuppetMasterPtr ControlStory(
      fidl::StringPtr story_id) {
    fuchsia::modular::StoryPuppetMasterPtr ptr;
    ptr_->ControlStory(story_id, ptr.NewRequest());
    return ptr;
  }

 protected:
  TestStoryCommandExecutor executor_;
  PuppetMasterImpl impl_;
  fuchsia::modular::PuppetMasterPtr ptr_;
};

TEST_F(PuppetMasterTest, CommandsAreSentToExecutor) {
  auto story = ControlStory("foo");

  // Enqueue some commands. Do this twice and show that all the commands show
  // up as one batch.
  fidl::VectorPtr<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story->Enqueue(std::move(commands));
  commands.push_back(MakeRemoveModCommand("two"));
  commands.push_back(MakeRemoveModCommand("three"));
  story->Enqueue(std::move(commands));

  // Commands are not run until Execute() is called.
  RunLoopUntilIdle();
  EXPECT_EQ(0, executor_.execute_count);

  fuchsia::modular::ExecuteResult result;
  bool got_result{false};
  // Instruct our test executor to return an OK status.
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr,
                                   "foo");
  story->Execute([&result, &got_result](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    got_result = true;
  });
  RunLoopUntilIdle();
  EXPECT_EQ(1, executor_.execute_count);
  EXPECT_TRUE(got_result);
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
  EXPECT_EQ("foo", result.story_id);

  EXPECT_EQ("foo", executor_.last_story_id);
  EXPECT_EQ(3u, executor_.last_commands.size());
  EXPECT_EQ("one", executor_.last_commands.at(0).remove_mod().mod_name->at(0));
  EXPECT_EQ("two", executor_.last_commands.at(1).remove_mod().mod_name->at(0));
  EXPECT_EQ("three",
            executor_.last_commands.at(2).remove_mod().mod_name->at(0));
}

TEST_F(PuppetMasterTest, NewStoryIdIsPreserved) {
  // Create a new story, and then execute some new commands on the same
  // connection. We should see that the StoryCommandExecutor receives the story
  // id that it reported after successful creation of the story on the last
  // execution.
  auto story = ControlStory(nullptr);

  fidl::VectorPtr<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr,
                                   "foo");
  story->Execute([](fuchsia::modular::ExecuteResult r) {});
  RunLoopUntilIdle();
  EXPECT_EQ(nullptr, executor_.last_story_id);

  // Execute more commands.
  commands.push_back(MakeRemoveModCommand("three"));
  story->Enqueue(std::move(commands));
  story->Execute([](fuchsia::modular::ExecuteResult r) {});
  RunLoopUntilIdle();
  EXPECT_EQ("foo", executor_.last_story_id);
}

TEST_F(PuppetMasterTest, NewStoriesAreKeptSeparate) {
  // Creating two new stories at the same time is OK and they are kept
  // separate.
  auto story1 = ControlStory(nullptr);
  auto story2 = ControlStory(nullptr);

  fidl::VectorPtr<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story1->Enqueue(std::move(commands));
  // We must run the loop to ensure that our message is dispatched.
  RunLoopUntilIdle();

  commands.push_back(MakeRemoveModCommand("two"));
  story2->Enqueue(std::move(commands));
  RunLoopUntilIdle();

  fuchsia::modular::ExecuteResult result;
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr,
                                   "story1");
  story1->Execute(
      [&result](fuchsia::modular::ExecuteResult r) { result = std::move(r); });
  RunLoopUntilIdle();
  EXPECT_EQ(1, executor_.execute_count);
  EXPECT_EQ("story1", result.story_id);
  EXPECT_EQ(1u, executor_.last_commands.size());
  EXPECT_EQ("one", executor_.last_commands.at(0).remove_mod().mod_name->at(0));

  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr,
                                   "story2");
  story2->Execute(
      [&result](fuchsia::modular::ExecuteResult r) { result = std::move(r); });
  RunLoopUntilIdle();
  EXPECT_EQ(2, executor_.execute_count);
  EXPECT_EQ("story2", result.story_id);
  EXPECT_EQ(1u, executor_.last_commands.size());
  EXPECT_EQ("two", executor_.last_commands.at(0).remove_mod().mod_name->at(0));
}

TEST_F(PuppetMasterTest, ExistingStoriesAreKeptSeparate) {
  // Creating two new stories at the same time is OK and they are kept
  // separate.
  auto story1 = ControlStory("foo");
  auto story2 = ControlStory("foo");

  fidl::VectorPtr<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story1->Enqueue(std::move(commands));
  // We must run the loop to ensure that our message is dispatched.
  RunLoopUntilIdle();

  commands.push_back(MakeRemoveModCommand("two"));
  story2->Enqueue(std::move(commands));
  RunLoopUntilIdle();

  fuchsia::modular::ExecuteResult result;
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr,
                                   "foo");
  story1->Execute(
      [&result](fuchsia::modular::ExecuteResult r) { result = std::move(r); });
  RunLoopUntilIdle();
  EXPECT_EQ(1, executor_.execute_count);
  EXPECT_EQ("foo", result.story_id);
  EXPECT_EQ(1u, executor_.last_commands.size());
  EXPECT_EQ("one", executor_.last_commands.at(0).remove_mod().mod_name->at(0));

  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK, nullptr,
                                   "foo");
  story2->Execute(
      [&result](fuchsia::modular::ExecuteResult r) { result = std::move(r); });
  RunLoopUntilIdle();
  EXPECT_EQ(2, executor_.execute_count);
  EXPECT_EQ("foo", result.story_id);
  EXPECT_EQ(1u, executor_.last_commands.size());
  EXPECT_EQ("two", executor_.last_commands.at(0).remove_mod().mod_name->at(0));
}

}  // namespace
}  // namespace modular
