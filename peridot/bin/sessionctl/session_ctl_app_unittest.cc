// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionctl/session_ctl_app.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "gtest/gtest.h"
#include "peridot/bin/sessionctl/session_ctl_constants.h"
#include "peridot/bin/sessionmgr/puppet_master/puppet_master_impl.h"
#include "peridot/lib/testing/test_story_command_executor.h"
#include "peridot/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

class SessionCtlAppTest : public testing::TestWithSessionStorage {
 public:
  SessionCtlAppTest() {
    session_storage_ = MakeSessionStorage("page");
    puppet_master_impl_ =
        std::make_unique<PuppetMasterImpl>(session_storage_.get(), &test_executor_);
    puppet_master_impl_->Connect(puppet_master_.NewRequest());
    done_ = false;
  }

 protected:
  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<PuppetMasterImpl> puppet_master_impl_;
  fuchsia::modular::PuppetMasterPtr puppet_master_;
  std::unique_ptr<Logger> logger_;
  testing::TestStoryCommandExecutor test_executor_;
  bool done_;

  SessionCtlApp CreateSessionCtl(fxl::CommandLine command_line) {
    logger_ = std::make_unique<Logger>(command_line.HasOption(kJsonOutFlagString));
    SessionCtlApp sessionctl(nullptr /* basemgr */, puppet_master_impl_.get(), *(logger_.get()),
                             async_get_default_dispatcher());
    return sessionctl;
  }

  void RunLoopUntilCommandExecutes(fit::function<void()> command) {
    done_ = false;
    command();
    RunLoopUntil([&] { return done_; });
  }

  fuchsia::modular::internal::StoryDataPtr GetStoryData(const std::string& story_id) {
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
      {kSessionCtlString, "--mod_name=mod", "--story_name=story", "--mod_url=foo"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);

  // Try to execute an invalid command
  sessionctl.ExecuteCommand("fake_cmd", command_line,
                            [](std::string error) { EXPECT_EQ(kGetUsageErrorString, error); });
}

TEST_F(SessionCtlAppTest, AddMod) {
  // Add a mod
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kAddModCommandString,
                                           "fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });
  std::string default_name_hash = std::to_string(
      std::hash<std::string>{}("fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx"));

  // Assert the story and the mod were added with default story and mod names
  auto story_data = GetStoryData(default_name_hash);
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
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kAddModCommandString, "mod_name"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });
  std::string default_name_hash = std::to_string(
      std::hash<std::string>{}("fuchsia-pkg://fuchsia.com/mod_name#meta/mod_name.cmx"));
  auto story_data = GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/mod_name#meta/mod_name.cmx",
            test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModWithColon) {
  // Add a mod
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, kAddModCommandString, "mod_name:0000"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });
  std::string default_name_hash = std::to_string(std::hash<std::string>{}("mod_name:0000"));
  auto story_data = GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ("mod_name:0000", test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModBadChars) {
  // Add a mod
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, kAddModCommandString, "a:bad/mod/name"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });
  std::string default_name_hash = std::to_string(std::hash<std::string>{}("a:bad/mod/name"));

  auto story_data = GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ("a:bad/mod/name", test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModWeb) {
  // Add a mod
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, kAddModCommandString, "https://www.google.com"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });

  // Assert the story and the mod were added with default story and mod names
  std::string default_name_hash =
      std::to_string(std::hash<std::string>{}("https://www.google.com"));
  auto story_data = GetStoryData(default_name_hash);
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
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, "--story_name=s", "--mod_name=m", kAddModCommandString, "mod_url"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });

  // Assert the story and the mod were added with overriden story and mod names
  auto story_name = "s";
  auto story_data = GetStoryData(story_name);
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
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, "--story_name=??", "--mod_name=m", kAddModCommandString, "mod_url"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  sessionctl.ExecuteCommand(kAddModCommandString, command_line, [](std::string error) {
    EXPECT_EQ("Bad characters in story_name: ??", error);
  });
}

TEST_F(SessionCtlAppTest, AddModMissingModUrl) {
  // Attempt to add a mod without a mod url
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kAddModCommandString});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  sessionctl.ExecuteCommand(kAddModCommandString, command_line, [](std::string error) {
    EXPECT_EQ("Missing MOD_URL. Ex: sessionctl add_mod slider_mod", error);
  });
}

TEST_F(SessionCtlAppTest, RemoveMod) {
  // Add a mod
  auto mod = "mod";
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kAddModCommandString, mod});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });

  // Remove the mod
  command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kRemoveModCommandString, mod});
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kRemoveModCommandString, command_line,
                                     [this](std::string error) {
                                       done_ = true;
                                       EXPECT_TRUE(error.empty());
                                     });
  });

  // Assert session_storage still contains the story
  std::string mod_package_name = fxl::StringPrintf(kFuchsiaPkgPath, mod, mod);
  std::string mod_hash = std::to_string(std::hash<std::string>{}(mod_package_name));
  auto story_data = GetStoryData(mod_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(mod_hash, story_data->story_name());
  EXPECT_EQ(mod_package_name,
            test_executor_.last_commands().at(0).remove_mod().mod_name_transitional);
  EXPECT_EQ(2, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, RemoveModOverrideDefault) {
  // Add a mod with overridden story and mod names
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, "--story_name=s", "--mod_name=m", kAddModCommandString, "mod"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });

  // Remove the mod with an overridden story name
  auto mod_name = "m";
  command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, "--story_name=s", kRemoveModCommandString, mod_name});
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kRemoveModCommandString, command_line,
                                     [this](std::string error) {
                                       done_ = true;
                                       EXPECT_TRUE(error.empty());
                                     });
  });

  // Assert session_storage still contains the story
  auto story_name = "s";
  auto story_data = GetStoryData(story_name);
  std::string mod_package_name = fxl::StringPrintf(kFuchsiaPkgPath, mod_name, mod_name);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(story_name, story_data->story_name());
  EXPECT_EQ(mod_package_name,
            test_executor_.last_commands().at(0).remove_mod().mod_name_transitional);
  EXPECT_EQ(2, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, RemoveModMissingModName) {
  // Attempt to remove a mod without a mod name
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kRemoveModCommandString});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  sessionctl.ExecuteCommand(kRemoveModCommandString, command_line, [](std::string error) {
    EXPECT_EQ("Missing MOD_NAME. Ex: sessionctl remove_mod slider_mod", error);
  });
}

TEST_F(SessionCtlAppTest, DeleteStory) {
  // Add a mod with overridden story name
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, "--story_name=story", kAddModCommandString, "mod"});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });

  // Remove the story
  auto story_name = "story";
  command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, kDeleteStoryCommandString, story_name});
  RunLoopUntilCommandExecutes([&] {
    sessionctl.ExecuteCommand(kDeleteStoryCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });

  auto story_data = GetStoryData(story_name);
  EXPECT_FALSE(story_data);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, DeleteStoryMissingStoryName) {
  // Attempt to delete a story without the required flags
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kDeleteStoryCommandString});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  sessionctl.ExecuteCommand(kDeleteStoryCommandString, command_line, [](std::string error) {
    EXPECT_EQ("Missing STORY_NAME. Ex. sessionctl delete_story story", error);
  });
}

TEST_F(SessionCtlAppTest, DeleteStoryBadName) {
  auto story_name = "bad_story";
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, kDeleteStoryCommandString, story_name});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);

  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kDeleteStoryCommandString, command_line,
                                     [this](std::string error) {
                                       done_ = true;
                                       EXPECT_EQ("Non-existent story_name bad_story", error);
                                     });
  });
}

TEST_F(SessionCtlAppTest, DeleteAllStories) {
  // Add two mods
  auto mod1 = "mod1";
  auto story1 =
      std::to_string(std::hash<std::string>{}("fuchsia-pkg://fuchsia.com/mod1#meta/mod1.cmx"));
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kAddModCommandString, mod1});
  SessionCtlApp sessionctl = CreateSessionCtl(command_line);
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });

  auto mod2 = "mod2";
  auto story2 =
      std::to_string(std::hash<std::string>{}("fuchsia-pkg://fuchsia.com/mod2#meta/mod2.cmx"));
  command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kAddModCommandString, mod2});
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kAddModCommandString, command_line, [this](std::string error) {
      done_ = true;
      EXPECT_TRUE(error.empty());
    });
  });

  EXPECT_TRUE(GetStoryData(story1));
  EXPECT_TRUE(GetStoryData(story2));

  // Delete all stories
  command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kDeleteAllStoriesCommandString});
  RunLoopUntilCommandExecutes([&] {
    return sessionctl.ExecuteCommand(kDeleteAllStoriesCommandString, command_line,
                                     [this](std::string error) {
                                       done_ = true;
                                       EXPECT_TRUE(error.empty());
                                     });
  });

  EXPECT_FALSE(GetStoryData(story1));
  EXPECT_FALSE(GetStoryData(story2));
}

}  // namespace
}  // namespace modular
