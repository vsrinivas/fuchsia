// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionctl/session_ctl_app.h"

#include <fuchsia/modular/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/modular/bin/sessionctl/session_ctl_constants.h"
#include "src/modular/bin/sessionmgr/puppet_master/puppet_master_impl.h"
#include "src/modular/lib/testing/test_story_command_executor.h"
#include "src/modular/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

class SessionCtlAppTest : public modular_testing::TestWithSessionStorage {
 public:
  SessionCtlAppTest() {
    session_storage_ = MakeSessionStorage();
    puppet_master_impl_ =
        std::make_unique<PuppetMasterImpl>(session_storage_.get(), &test_executor_);
    puppet_master_impl_->Connect(puppet_master_.NewRequest());
  }

 protected:
  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<PuppetMasterImpl> puppet_master_impl_;
  fuchsia::modular::internal::BasemgrDebugPtr basemgr_;
  fuchsia::modular::PuppetMasterPtr puppet_master_;
  std::unique_ptr<Logger> logger_;
  modular_testing::TestStoryCommandExecutor test_executor_;

  SessionCtlApp CreateSessionCtl() {
    logger_ = std::make_unique<Logger>(/*json_output=*/false);
    SessionCtlApp sessionctl(nullptr /* basemgr */, puppet_master_impl_.get(), *(logger_.get()),
                             async_get_default_dispatcher());
    return sessionctl;
  }

  std::string RunSessionCtlCommand(SessionCtlApp* app, std::vector<std::string> args) {
    auto commandline =
        fxl::CommandLineFromIteratorsWithArgv0(kSessionCtlString, args.begin(), args.end());
    auto command = commandline.positional_args()[0];
    std::string error_out;
    bool done = false;
    app->ExecuteCommand(command, commandline, [&](std::string error) {
      done = true;
      error_out = error;
    });

    RunLoopUntil([&] { return done; });
    return error_out;
  }
};

TEST_F(SessionCtlAppTest, GetUsage) {
  SessionCtlApp sessionctl = CreateSessionCtl();

  // Try to execute an invalid command
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, "--mod_name=mod", "--story_name=story", "--mod_url=foo"});
  sessionctl.ExecuteCommand("fake_cmd", command_line,
                            [](std::string error) { EXPECT_EQ(kGetUsageErrorString, error); });
}

TEST_F(SessionCtlAppTest, AddMod) {
  // Add a mod
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("", RunSessionCtlCommand(
                    &sessionctl,
                    {kAddModCommandString, "fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx"}));
  std::string default_name_hash = std::to_string(
      std::hash<std::string>{}("fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx"));

  // Assert the story and the mod were added with default story and mod names
  auto story_data = session_storage_->GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx",
            test_executor_.last_commands().at(0).add_mod().mod_name_transitional);
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx",
            test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModWithoutURL) {
  // Add a mod
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("", RunSessionCtlCommand(&sessionctl, {kAddModCommandString, "mod_name"}));
  std::string default_name_hash = std::to_string(
      std::hash<std::string>{}("fuchsia-pkg://fuchsia.com/mod_name#meta/mod_name.cmx"));
  auto story_data = session_storage_->GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/mod_name#meta/mod_name.cmx",
            test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModWithColon) {
  // Add a mod
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("", RunSessionCtlCommand(&sessionctl, {kAddModCommandString, "mod_name:0000"}));
  std::string default_name_hash = std::to_string(std::hash<std::string>{}("mod_name:0000"));
  auto story_data = session_storage_->GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ("mod_name:0000", test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModBadChars) {
  // Add a mod
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("", RunSessionCtlCommand(&sessionctl, {kAddModCommandString, "a:bad/mod/name"}));
  std::string default_name_hash = std::to_string(std::hash<std::string>{}("a:bad/mod/name"));

  auto story_data = session_storage_->GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ("a:bad/mod/name", test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModWeb) {
  // Add a mod
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("",
            RunSessionCtlCommand(&sessionctl, {kAddModCommandString, "https://www.google.com"}));

  // Assert the story and the mod were added with default story and mod names
  std::string default_name_hash =
      std::to_string(std::hash<std::string>{}("https://www.google.com"));
  auto story_data = session_storage_->GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ("https://www.google.com",
            test_executor_.last_commands().at(0).add_mod().mod_name_transitional);
  EXPECT_EQ("https://www.google.com",
            test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModOverrideDefaults) {
  // Add a mod
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("", RunSessionCtlCommand(&sessionctl, {"--story_name=s", "--mod_name=m",
                                                   kAddModCommandString, "mod_url"}));

  // Assert the story and the mod were added with overriden story and mod names
  auto story_name = "s";
  auto story_data = session_storage_->GetStoryData(story_name);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(story_name, story_data->story_name());
  EXPECT_EQ("m", test_executor_.last_commands().at(0).add_mod().mod_name_transitional);
  // mod_url becomes package
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx",
            test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModBadStoryName) {
  // Add a mod
  SessionCtlApp sessionctl = CreateSessionCtl();
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, "--story_name=??", "--mod_name=m", kAddModCommandString, "mod_url"});
  sessionctl.ExecuteCommand(kAddModCommandString, command_line, [](std::string error) {
    EXPECT_EQ("Bad characters in story_name: ??", error);
  });
}

TEST_F(SessionCtlAppTest, AddModMissingModUrl) {
  // Attempt to add a mod without a mod url
  SessionCtlApp sessionctl = CreateSessionCtl();
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kAddModCommandString});
  sessionctl.ExecuteCommand(kAddModCommandString, command_line, [](std::string error) {
    EXPECT_EQ("Missing MOD_URL. Ex: sessionctl add_mod slider_mod", error);
  });
}

TEST_F(SessionCtlAppTest, RemoveMod) {
  SessionCtlApp sessionctl = CreateSessionCtl();

  // Add a mod
  auto mod = "mod";
  EXPECT_EQ("", RunSessionCtlCommand(&sessionctl, {kAddModCommandString, mod}));

  // Remove the mod
  EXPECT_EQ("", RunSessionCtlCommand(&sessionctl, {kRemoveModCommandString, mod}));

  // Assert session_storage still contains the story
  std::string mod_package_name = fxl::StringPrintf(kFuchsiaPkgPath, mod, mod);
  std::string mod_hash = std::to_string(std::hash<std::string>{}(mod_package_name));
  auto story_data = session_storage_->GetStoryData(mod_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(mod_hash, story_data->story_name());
  EXPECT_EQ(mod_package_name,
            test_executor_.last_commands().at(0).remove_mod().mod_name_transitional.value_or(""));
  EXPECT_EQ(2, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, RemoveModOverrideDefault) {
  // Add a mod with overridden story and mod names
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("", RunSessionCtlCommand(
                    &sessionctl, {"--story_name=s", "--mod_name=m", kAddModCommandString, "mod"}));

  // Remove the mod from story "s"
  auto mod_name = "m";
  EXPECT_EQ(
      "", RunSessionCtlCommand(&sessionctl, {"--story_name=s", kRemoveModCommandString, mod_name}));

  // Assert session_storage still contains the story
  auto story_data = session_storage_->GetStoryData("s");
  std::string mod_package_name = fxl::StringPrintf(kFuchsiaPkgPath, mod_name, mod_name);
  ASSERT_TRUE(story_data);
  EXPECT_EQ("s", story_data->story_name());
  EXPECT_EQ(mod_package_name,
            test_executor_.last_commands().at(0).remove_mod().mod_name_transitional.value_or(""));
  EXPECT_EQ(2, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, RemoveModMissingModName) {
  // Attempt to remove a mod without a mod name
  SessionCtlApp sessionctl = CreateSessionCtl();
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kRemoveModCommandString});
  sessionctl.ExecuteCommand(kRemoveModCommandString, command_line, [](std::string error) {
    EXPECT_EQ("Missing MOD_NAME. Ex: sessionctl remove_mod slider_mod", error);
  });
}

TEST_F(SessionCtlAppTest, DeleteStory) {
  // Add a mod with overridden story name
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("",
            RunSessionCtlCommand(&sessionctl, {"--story_name=story", kAddModCommandString, "mod"}));

  // Remove the story
  auto story_name = "story";
  EXPECT_EQ("", RunSessionCtlCommand(&sessionctl, {kDeleteStoryCommandString, story_name}));

  auto story_data = session_storage_->GetStoryData(story_name);
  EXPECT_FALSE(story_data);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, DeleteStoryMissingStoryName) {
  // Attempt to delete a story without the required flags
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("Missing STORY_NAME. Ex. sessionctl delete_story story",
            RunSessionCtlCommand(&sessionctl, {kDeleteStoryCommandString}));
}

TEST_F(SessionCtlAppTest, DeleteStoryBadName) {
  auto story_name = "bad_story";
  auto command_line = fxl::CommandLineFromInitializerList({
      kSessionCtlString,
  });
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("Non-existent story_name bad_story",
            RunSessionCtlCommand(&sessionctl, {kDeleteStoryCommandString, story_name}));
}

TEST_F(SessionCtlAppTest, DeleteAllStories) {
  // Add two mods
  auto mod1 = "mod1";
  auto story1 =
      std::to_string(std::hash<std::string>{}("fuchsia-pkg://fuchsia.com/mod1#meta/mod1.cmx"));
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kAddModCommandString, mod1});
  SessionCtlApp sessionctl = CreateSessionCtl();
  EXPECT_EQ("", RunSessionCtlCommand(&sessionctl, {kAddModCommandString, mod1}));

  auto mod2 = "mod2";
  auto story2 =
      std::to_string(std::hash<std::string>{}("fuchsia-pkg://fuchsia.com/mod2#meta/mod2.cmx"));
  EXPECT_EQ("", RunSessionCtlCommand(&sessionctl, {kAddModCommandString, mod2}));

  EXPECT_TRUE(session_storage_->GetStoryData(story1));
  EXPECT_TRUE(session_storage_->GetStoryData(story2));

  // Delete all stories
  EXPECT_EQ("", RunSessionCtlCommand(&sessionctl, {kDeleteAllStoriesCommandString}));
  return;

  EXPECT_FALSE(session_storage_->GetStoryData(story1));
  EXPECT_FALSE(session_storage_->GetStoryData(story2));
}

}  // namespace
}  // namespace modular
