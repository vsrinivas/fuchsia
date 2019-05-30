// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/puppet_master_impl.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "gtest/gtest.h"
#include "peridot/lib/testing/test_story_command_executor.h"
#include "peridot/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

fuchsia::modular::StoryCommand MakeRemoveModCommand(std::string mod_name) {
  fuchsia::modular::StoryCommand command;
  fuchsia::modular::RemoveMod remove_mod;
  remove_mod.mod_name_transitional = mod_name;
  command.set_remove_mod(std::move(remove_mod));
  return command;
}

class PuppetMasterTest : public testing::TestWithSessionStorage {
 public:
  void SetUp() override {
    TestWithSessionStorage::SetUp();
    storage_ = MakeSessionStorage("page");
    impl_ = std::make_unique<PuppetMasterImpl>(storage_.get(), &executor_);
    impl_->Connect(ptr_.NewRequest());
  }

  fuchsia::modular::StoryPuppetMasterPtr ControlStory(
      fidl::StringPtr story_name) {
    fuchsia::modular::StoryPuppetMasterPtr ptr;
    ptr_->ControlStory(story_name, ptr.NewRequest());
    return ptr;
  }

 protected:
  testing::TestStoryCommandExecutor executor_;
  std::unique_ptr<SessionStorage> storage_;
  std::unique_ptr<PuppetMasterImpl> impl_;
  fuchsia::modular::PuppetMasterPtr ptr_;
};

TEST_F(PuppetMasterTest, CommandsAreSentToExecutor) {
  // This should create a new story in StoryStorage called "foo".
  auto story = ControlStory("foo");

  // Enqueue some commands. Do this twice and show that all the commands show
  // up as one batch.
  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story->Enqueue(std::move(commands));
  commands.push_back(MakeRemoveModCommand("two"));
  commands.push_back(MakeRemoveModCommand("three"));
  story->Enqueue(std::move(commands));

  // Commands are not run until Execute() is called.
  RunLoopUntilIdle();
  EXPECT_EQ(0, executor_.execute_count());

  fuchsia::modular::ExecuteResult result;
  bool done{false};
  // Instruct our test executor to return an OK status.
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK,
                                   nullptr);
  story->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    done = true;
  });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(1, executor_.execute_count());
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);

  EXPECT_EQ("foo", executor_.last_story_id());
  ASSERT_EQ(3u, executor_.last_commands().size());
  EXPECT_EQ("one",
            executor_.last_commands().at(0).remove_mod().mod_name_transitional);
  EXPECT_EQ("two",
            executor_.last_commands().at(1).remove_mod().mod_name_transitional);
  EXPECT_EQ("three",
            executor_.last_commands().at(2).remove_mod().mod_name_transitional);
}

TEST_F(PuppetMasterTest, CommandsAreSentToExecutor_IfWeCloseStoryChannel) {
  // We're going to call Execute(), and then immediately drop the
  // StoryPuppetMaster connection. We won't get a callback, but we still
  // expect that the commands are executed.
  auto story = ControlStory("foo");

  // Enqueue some commands. Do this twice and show that all the commands show
  // up as one batch.
  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story->Enqueue(std::move(commands));

  fuchsia::modular::ExecuteResult result;
  bool callback_called{false};
  // Instruct our test executor to return an OK status.
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK,
                                   nullptr);
  story->Execute(
      [&](fuchsia::modular::ExecuteResult r) { callback_called = true; });
  story.Unbind();
  RunLoopUntil([&]() { return executor_.execute_count() > 0; });
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(1, executor_.execute_count());
}

TEST_F(PuppetMasterTest, MultipleExecuteCalls) {
  // Create a new story, and then execute some new commands on the same
  // connection. We should see that the StoryCommandExecutor receives the story
  // id that it reported after successful creation of the story on the last
  // execution.
  auto story = ControlStory("foo");

  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK,
                                   nullptr);
  bool done{false};
  story->Execute([&](fuchsia::modular::ExecuteResult r) { done = true; });
  RunLoopUntil([&]() { return done; });
  auto story_id = executor_.last_story_id();

  // Execute more commands.
  commands.push_back(MakeRemoveModCommand("three"));
  story->Enqueue(std::move(commands));
  done = false;
  story->Execute([&](fuchsia::modular::ExecuteResult r) { done = true; });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(story_id, executor_.last_story_id());
}

TEST_F(PuppetMasterTest, NewStoriesAreKeptSeparate) {
  // Creating two new stories at the same time is OK and they are kept
  // separate.
  auto story1 = ControlStory("story1");
  auto story2 = ControlStory("story2");

  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story1->Enqueue(std::move(commands));
  // We must run the loop to ensure that our message is dispatched.
  RunLoopUntilIdle();

  commands.push_back(MakeRemoveModCommand("two"));
  story2->Enqueue(std::move(commands));
  RunLoopUntilIdle();

  fuchsia::modular::ExecuteResult result;
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK,
                                   nullptr);
  bool done{false};
  story1->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    done = true;
  });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(1, executor_.execute_count());
  auto story1_id = executor_.last_story_id();
  ASSERT_EQ(1u, executor_.last_commands().size());
  EXPECT_EQ("one",
            executor_.last_commands().at(0).remove_mod().mod_name_transitional);

  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK,
                                   nullptr);
  done = false;
  story2->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    done = true;
  });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(2, executor_.execute_count());
  auto story2_id = executor_.last_story_id();
  ASSERT_EQ(1u, executor_.last_commands().size());
  EXPECT_EQ("two",
            executor_.last_commands().at(0).remove_mod().mod_name_transitional);

  // The two IDs should be different, because we gave the two stories different
  // names.
  EXPECT_NE(story1_id, story2_id);
}

TEST_F(PuppetMasterTest, ControlExistingStory) {
  // Controlling the same story from two connections is OK. The first call to
  // Execute() will create the story, and the second will re-use the same story
  // record.
  auto story1 = ControlStory("foo");
  auto story2 = ControlStory("foo");

  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story1->Enqueue(std::move(commands));
  // We must run the loop to ensure that our message is dispatched.
  RunLoopUntilIdle();

  commands.push_back(MakeRemoveModCommand("two"));
  story2->Enqueue(std::move(commands));
  RunLoopUntilIdle();

  fuchsia::modular::ExecuteResult result;
  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK,
                                   nullptr);
  bool done{false};
  story1->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    done = true;
  });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(1, executor_.execute_count());
  auto story_id = executor_.last_story_id();
  ASSERT_EQ(1u, executor_.last_commands().size());
  EXPECT_EQ("one",
            executor_.last_commands().at(0).remove_mod().mod_name_transitional);

  executor_.SetExecuteReturnResult(fuchsia::modular::ExecuteStatus::OK,
                                   nullptr);
  done = false;
  story2->Execute([&](fuchsia::modular::ExecuteResult r) {
    result = std::move(r);
    done = true;
  });
  RunLoopUntil([&]() { return done; });
  EXPECT_EQ(2, executor_.execute_count());
  EXPECT_EQ(story_id, executor_.last_story_id());
  ASSERT_EQ(1u, executor_.last_commands().size());
  EXPECT_EQ("two",
            executor_.last_commands().at(0).remove_mod().mod_name_transitional);
}

TEST_F(PuppetMasterTest, CreateStoryWithOptions) {
  // Verify that options are set when the story is created (as result of an
  // execution) and are not updated in future executions.
  auto story = ControlStory("foo");

  fuchsia::modular::StoryOptions options;
  options.kind_of_proto_story = true;
  story->SetCreateOptions(std::move(options));

  // Enqueue some commands.
  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story->Enqueue(std::move(commands));

  // Options are not set until execute that triggers the creation of a story.
  bool done{};
  storage_->GetStoryData("foo")->Then(
      [&](fuchsia::modular::internal::StoryDataPtr data) {
        EXPECT_EQ(nullptr, data);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  done = false;
  story->Execute([&](fuchsia::modular::ExecuteResult result) {
    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
    done = true;
  });
  RunLoopUntil([&] { return done; });
  auto story_id = executor_.last_story_id();

  // Options should have been set.
  done = false;
  storage_->GetStoryData("foo")->Then(
      [&](fuchsia::modular::internal::StoryDataPtr data) {
        EXPECT_TRUE(data->story_options().kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Setting new options and executing again should have no effect.
  fuchsia::modular::StoryOptions options2;
  options2.kind_of_proto_story = false;
  story->SetCreateOptions(std::move(options2));

  // Enqueue some commands.
  std::vector<fuchsia::modular::StoryCommand> commands2;
  commands2.push_back(MakeRemoveModCommand("two"));
  story->Enqueue(std::move(commands2));

  done = false;
  story->Execute([&](fuchsia::modular::ExecuteResult result) {
    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
    done = true;
  });
  RunLoopUntil([&] { return done; });

  EXPECT_EQ(story_id, executor_.last_story_id());

  // Options should not have changed.
  done = false;
  storage_->GetStoryData("foo")->Then(
      [&](fuchsia::modular::internal::StoryDataPtr data) {
        EXPECT_TRUE(data->story_options().kind_of_proto_story);
        done = true;
      });

  RunLoopUntil([&] { return done; });
}

// Verifies that the StoryInfo extra field is set when calling SetStoryInfoExtra
// prior to creating the story and executing story commands
TEST_F(PuppetMasterTest, DISABLED_CreateStoryWithStoryInfoExtra) {
  const auto story_name = "story_info_extra_1";
  const auto extra_entry_key = "test_key";
  const auto extra_entry_value = "test_value";

  auto story = ControlStory(story_name);

  std::vector<fuchsia::modular::StoryInfoExtraEntry> extra_info{
      fuchsia::modular::StoryInfoExtraEntry{
          .key = extra_entry_key,
          .value = extra_entry_value,
      }};
  const auto extra_info_size =
      extra_info.size();  // For convenience, to avoid a wordy cast to
                          // size_type in ASSERT_EQ below.

  // Try to SetStoryInfoExtra. It should succeed because the story has not been
  // created yet.
  bool done{};
  story->SetStoryInfoExtra(
      std::move(extra_info),
      [&](fuchsia::modular::StoryPuppetMaster_SetStoryInfoExtra_Result result) {
        EXPECT_FALSE(result.is_err());
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Enqueue some commands.
  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story->Enqueue(std::move(commands));

  // The story, and its StoryData, does not exist until the story is created,
  // which is after the commands are executed.
  storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
        EXPECT_EQ(nullptr, data);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Execute the commands, implicitly creating the story.
  done = false;
  story->Execute([&](fuchsia::modular::ExecuteResult result) {
    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
    done = true;
  });
  RunLoopUntil([&] { return done; });

  // StoryData should contain the StoryInfo extra value that was set previously
  done = false;
  storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
        ASSERT_FALSE(data->story_info().extra.is_null());
        auto extra_info = data->story_info().extra.get();
        ASSERT_EQ(extra_info.size(), extra_info_size);
        EXPECT_EQ(extra_info.at(0).key, extra_entry_key);
        EXPECT_EQ(extra_info.at(0).value, extra_entry_value);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

// Verifies that calls to SetStoryInfoExtra after the story is created
// do not modify the original value.
TEST_F(PuppetMasterTest, SetStoryInfoExtraAfterCreateStory) {
  const auto story_name = "story_info_extra_2";

  auto story = ControlStory(story_name);

  // Enqueue some commands.
  std::vector<fuchsia::modular::StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  story->Enqueue(std::move(commands));

  // The story, and its StoryData, does not exist until the story is created,
  // which is after the commands are executed.
  bool done{};
  storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
        EXPECT_EQ(nullptr, data);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Execute the commands, implicitly creating the story.
  done = false;
  story->Execute([&](fuchsia::modular::ExecuteResult result) {
    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
    done = true;
  });
  RunLoopUntil([&] { return done; });
  auto story_id = executor_.last_story_id();

  // Calling SetStoryInfoExtra again and executing again should not modify
  // the original, unset value.
  std::vector<fuchsia::modular::StoryInfoExtraEntry> extra_info{
      fuchsia::modular::StoryInfoExtraEntry{
          .key = "ignored_key",
          .value = "ignored_value",
      }};

  // Try to SetStoryInfoExtra. It should return an error because the story has
  // already been created.
  done = false;
  story->SetStoryInfoExtra(
      std::move(extra_info),
      [&](fuchsia::modular::StoryPuppetMaster_SetStoryInfoExtra_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(
            result.err(),
            fuchsia::modular::ConfigureStoryError::ERR_STORY_ALREADY_CREATED);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Enqueue and execute some commands.
  std::vector<fuchsia::modular::StoryCommand> commands2;
  commands2.push_back(MakeRemoveModCommand("two"));
  story->Enqueue(std::move(commands2));

  done = false;
  story->Execute([&](fuchsia::modular::ExecuteResult result) {
    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
    done = true;
  });
  RunLoopUntil([&] { return done; });

  // Ensure the commands have been executed on the story created earlier.
  EXPECT_EQ(story_id, executor_.last_story_id());

  // StoryInfo extra should be null because it was not set prior
  // to creating the story.
  done = false;
  storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr data) {
        ASSERT_TRUE(data->story_info().extra.is_null());
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

// Verifies that calls to SetStoryInfoExtra succeed after a story is deleted.
TEST_F(PuppetMasterTest, SetStoryInfoExtraAfterDeleteStory) {
  const auto story_name = "story_info_extra_3";

  // Create the story.
  bool done{};
  storage_
      ->CreateStory(story_name, /* extra_info= */ {}, /* story_options= */ {})
      ->Then([&](fidl::StringPtr id, fuchsia::ledger::PageId page_id) {
        done = true;
      });
  RunLoopUntil([&] { return done; });

  auto story = ControlStory(story_name);

  std::vector<fuchsia::modular::StoryInfoExtraEntry> extra_info{
      fuchsia::modular::StoryInfoExtraEntry{
          .key = "ignored_key",
          .value = "ignored_value",
      }};

  // Try to SetStoryInfoExtra. It should return an error because the story has
  // already been created.
  done = false;
  story->SetStoryInfoExtra(
      extra_info,
      [&](fuchsia::modular::StoryPuppetMaster_SetStoryInfoExtra_Result result) {
        EXPECT_TRUE(result.is_err());
        EXPECT_EQ(
            result.err(),
            fuchsia::modular::ConfigureStoryError::ERR_STORY_ALREADY_CREATED);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Delete the story.
  done = false;
  ptr_->DeleteStory(story_name, [&] { done = true; });
  RunLoopUntil([&] { return done; });

  // Try to SetStoryInfoExtra again. It should succeed because story it applies
  // to has not been created yet.
  done = false;
  story->SetStoryInfoExtra(
      extra_info,
      [&](fuchsia::modular::StoryPuppetMaster_SetStoryInfoExtra_Result result) {
        EXPECT_FALSE(result.is_err());
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(PuppetMasterTest, DeleteStory) {
  std::string story_id;

  // Create a story.
  storage_->CreateStory("foo", {} /* extra_info */, {} /* story_options */)
      ->Then([&](fidl::StringPtr id, fuchsia::ledger::PageId page_id) {
        story_id = id;
      });

  // Delete it
  bool done{};
  ptr_->DeleteStory("foo", [&] { done = true; });
  RunLoopUntil([&] { return done; });

  done = false;
  storage_->GetStoryData(story_id)->Then(
      [&](fuchsia::modular::internal::StoryDataPtr story_data) {
        EXPECT_EQ(story_data, nullptr);
        done = true;
      });

  RunLoopUntil([&] { return done; });
}

TEST_F(PuppetMasterTest, GetStories) {
  // Zero stories to should exist.
  bool done{};
  ptr_->GetStories([&](std::vector<std::string> story_names) {
    EXPECT_EQ(0u, story_names.size());
    done = true;
  });
  RunLoopUntil([&] { return done; });

  // Create a story.
  storage_->CreateStory("foo", {} /* extra_info */, {} /* story_options */);

  // "foo" should be listed.
  done = false;
  ptr_->GetStories([&](std::vector<std::string> story_names) {
    ASSERT_EQ(1u, story_names.size());
    EXPECT_EQ("foo", story_names.at(0));
    done = true;
  });
  RunLoopUntil([&] { return done; });
}

}  // namespace
}  // namespace modular
