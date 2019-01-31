// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionctl/session_ctl_app.h"
#include "peridot/bin/sessionctl/session_ctl_constants.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "gtest/gtest.h"
#include "peridot/bin/sessionmgr/puppet_master/puppet_master_impl.h"
#include "peridot/lib/testing/test_story_command_executor.h"
#include "peridot/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

class SessionCtlAppTest : public testing::TestWithSessionStorage {
 public:
  void SetUp() override {
    TestWithSessionStorage::SetUp();
    session_storage_ = MakeSessionStorage("page");
    puppet_master_impl_ = std::make_unique<PuppetMasterImpl>(
        session_storage_.get(), &test_executor_);
    puppet_master_impl_->Connect(puppet_master.NewRequest());
    done_ = false;
  }

 protected:
  fuchsia::modular::PuppetMasterPtr puppet_master;
  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<PuppetMasterImpl> puppet_master_impl_;
  std::unique_ptr<Logger> logger_;
  testing::TestStoryCommandExecutor test_executor_;
  bool done_;

  SessionCtlApp CreateSessionCtl(fxl::CommandLine command_line) {
    logger_ =
        std::make_unique<Logger>(command_line.HasOption(kJsonOutFlagString));
    SessionCtlApp sessionctl(nullptr /* basemgr */, puppet_master_impl_.get(),
                             *(logger_.get()), async_get_default_dispatcher(),
                             [&] { done_ = true; });
    return sessionctl;
  }

  std::string RunLoopUntilCommandExecutes(
      std::function<std::string()> command) {
    done_ = false;
    std::string error = command();

    RunLoopUntil([&] { return done_; });

    return error;
  }

  fuchsia::modular::internal::StoryDataPtr GetStoryData(
      const std::string& story_id) {
    fuchsia::modular::internal::StoryDataPtr sd;
    done_ = false;
    session_storage_->GetStoryData(story_id)->Then(
        [&](fuchsia::modular::internal::StoryDataPtr story_data) {
          sd = std::move(story_data);
          done_ = true;
        });
    RunLoopUntil([&] { return done_; });

    return sd;
  }
};

TEST_F(SessionCtlAppTest, GetUsage) {
  auto command_line = fxl::CommandLineFromInitializerList(
      {"sessionctl", "--mod_name=mod", "--story_name=story", "--mod_url=foo"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);

  // Try to execute an invalid command
  std::string error = sessionctl.ExecuteCommand("fake_cmd", command_line);
  RunLoopUntilIdle();

  EXPECT_EQ(kGetUsageErrorString, error);
}

TEST_F(SessionCtlAppTest, AddMod) {
  // Add a mod
  auto command_line = fxl::CommandLineFromInitializerList(
      {"sessionctl", "add_mod",
       "fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line);
  });

  // Assert the story and the mod were added with default story and mod names
  auto story_data = GetStoryData("mod_url");
  ASSERT_TRUE(story_data);
  EXPECT_EQ("mod_url", story_data->story_name);
  EXPECT_EQ("mod_url",
            test_executor_.last_commands().at(0).add_mod().mod_name.at(0));
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx",
            test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModOverrideDefaults) {
  // Add a mod
  auto command_line = fxl::CommandLineFromInitializerList(
      {"sessionctl", "--story_name=s", "--mod_name=m", "add_mod", "mod_url"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line);
  });

  // Assert the story and the mod were added with overriden story and mod names
  auto story_data = GetStoryData("s");
  ASSERT_TRUE(story_data);
  EXPECT_EQ("s", story_data->story_name);
  EXPECT_EQ("m", test_executor_.last_commands().at(0).add_mod().mod_name.at(0));
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx",
            test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModMissingModUrl) {
  // Attempt to add a mod without a mod url
  auto command_line =
      fxl::CommandLineFromInitializerList({"sessionctl", "add_mod"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  std::string error =
      sessionctl.ExecuteCommand(kAddModCommandString, command_line);

  RunLoopUntilIdle();
  EXPECT_EQ("Missing MOD_URL. Ex: sessionctl add_mod slider_mod", error);
}

TEST_F(SessionCtlAppTest, RemoveMod) {
  // Add a mod
  auto command_line =
      fxl::CommandLineFromInitializerList({"sessionctl", "add_mod", "mod"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line);
  });

  // Remove the mod
  command_line =
      fxl::CommandLineFromInitializerList({"sessionctl", "remove_mod", "mod"});
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kRemoveModCommandString, command_line);
  });

  // Assert session_storage still contains the story
  auto story_data = GetStoryData("mod");
  ASSERT_TRUE(story_data);
  EXPECT_EQ("mod", story_data->story_name);
  EXPECT_EQ("mod",
            test_executor_.last_commands().at(0).remove_mod().mod_name.at(0));
  EXPECT_EQ(2, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, RemoveModOverrideDefault) {
  // Add a mod with overridden story and mod names
  auto command_line = fxl::CommandLineFromInitializerList(
      {"sessionctl", "--story_name=s", "--mod_name=m", "add_mod", "mod"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line);
  });

  // Remove the mod with an overridden story name
  command_line = fxl::CommandLineFromInitializerList(
      {"sessionctl", "--story_name=s", "remove_mod", "m"});
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kRemoveModCommandString, command_line);
  });

  // Assert session_storage still contains the story
  auto story_data = GetStoryData("s");
  ASSERT_TRUE(story_data);
  EXPECT_EQ("s", story_data->story_name);
  EXPECT_EQ("m",
            test_executor_.last_commands().at(0).remove_mod().mod_name.at(0));
  EXPECT_EQ(2, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, RemoveModMissingModName) {
  // Attempt to remove a mod without a mod name
  auto command_line =
      fxl::CommandLineFromInitializerList({"sessionctl", "remove_mod"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  std::string error =
      sessionctl.ExecuteCommand(kRemoveModCommandString, command_line);

  RunLoopUntilIdle();
  EXPECT_EQ("Missing MOD_NAME. Ex: sessionctl remove_mod slider_mod", error);
}

TEST_F(SessionCtlAppTest, DeleteStory) {
  // Add a mod with overridden story name
  auto command_line = fxl::CommandLineFromInitializerList(
      {"sessionctl", "--story_name=story", "add_mod", "mod"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line);
  });

  // Remove the story
  command_line = fxl::CommandLineFromInitializerList(
      {"sessionctl", "delete_story", "story"});
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kDeleteStoryCommandString, command_line);
  });

  auto story_data = GetStoryData("mod");
  EXPECT_FALSE(story_data);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, DeleteStoryMissingStoryName) {
  // Attempt to delete a story without the required flags
  auto command_line =
      fxl::CommandLineFromInitializerList({"sessionctl", "delete_story"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  std::string error =
      sessionctl.ExecuteCommand(kDeleteStoryCommandString, command_line);

  RunLoopUntilIdle();
  EXPECT_EQ("Missing STORY_NAME. Ex. sessionctl delete_story story", error);
}

}  // namespace
}  // namespace modular
