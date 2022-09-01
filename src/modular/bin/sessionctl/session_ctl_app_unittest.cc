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

class FakeLoader : public fuchsia::sys::Loader {
 public:
  void LoadUrl(std::string url, LoadUrlCallback done) {
    if (urls_.count(url) == 0) {
      done(nullptr);
      return;
    }
    auto package = std::make_unique<fuchsia::sys::Package>();
    done(std::move(package));
  }

  void AddUrl(std::string url) { urls_.insert(url); }

  fuchsia::sys::LoaderPtr Connect() {
    fuchsia::sys::LoaderPtr loader;
    bindings_.AddBinding(this, loader.NewRequest());
    return loader;
  }

 private:
  std::set<std::string> urls_;
  fidl::BindingSet<fuchsia::sys::Loader> bindings_;
};

class SessionCtlAppTest : public modular_testing::TestWithSessionStorage {
 public:
  SessionCtlAppTest() {
    session_storage_ = MakeSessionStorage();
    puppet_master_impl_ =
        std::make_unique<PuppetMasterImpl>(session_storage_.get(), &test_executor_);
  }

 protected:
  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<PuppetMasterImpl> puppet_master_impl_;
  FakeLoader fake_loader_;
  fuchsia::modular::internal::BasemgrDebugPtr basemgr_;
  std::unique_ptr<Logger> logger_;
  modular_testing::TestStoryCommandExecutor test_executor_;

  std::unique_ptr<SessionCtlApp> CreateSessionCtl() {
    logger_ = std::make_unique<Logger>(/*json_output=*/false);
    fuchsia::modular::PuppetMasterPtr puppet_master;
    puppet_master_impl_->Connect(puppet_master.NewRequest());
    return std::make_unique<SessionCtlApp>(/*basemgr_debug=*/nullptr, std::move(puppet_master),
                                           fake_loader_.Connect(), *(logger_.get()),
                                           async_get_default_dispatcher());
  }

  SessionCtlApp::CommandResult RunSessionCtlCommand(SessionCtlApp* app,
                                                    std::vector<std::string> args) {
    auto commandline =
        fxl::CommandLineFromIteratorsWithArgv0(kSessionCtlString, args.begin(), args.end());
    auto command = commandline.positional_args()[0];
    SessionCtlApp::CommandResult result_out;
    bool done = false;
    app->ExecuteCommand(command, commandline, [&](SessionCtlApp::CommandResult result) {
      done = true;
      result_out = std::move(result);
    });

    RunLoopUntil([&] { return done; });
    return result_out;
  }
};

TEST_F(SessionCtlAppTest, GetUsage) {
  auto sessionctl = CreateSessionCtl();

  // Try to execute an invalid command
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, "--mod_name=mod", "--story_name=story", "--mod_url=foo"});
  sessionctl->ExecuteCommand("fake_cmd", command_line, [](SessionCtlApp::CommandResult result) {
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ("", result.error());
  });
}

TEST_F(SessionCtlAppTest, AddMod_ExistingFuchsiaPkg) {
  // Add a mod URL starting with fuchsia-pkg:// that exists.
  constexpr char kModUrl[] = "fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx";
  fake_loader_.AddUrl(kModUrl);

  auto sessionctl = CreateSessionCtl();
  EXPECT_TRUE(RunSessionCtlCommand(sessionctl.get(), {kAddModCommandString, kModUrl}).is_ok());
  std::string default_name_hash = std::to_string(std::hash<std::string>{}(kModUrl));

  // Assert the story and the mod were added with default story and mod names
  auto story_data = session_storage_->GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ(kModUrl, test_executor_.last_commands().at(0).add_mod().mod_name_transitional);
  EXPECT_EQ(kModUrl, test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddMod_NonExistingFuchsiaPkg) {
  // Add a mod URL starting with fuchsia-pkg:// that does not exist.
  constexpr char kModUrl[] = "fuchsia-pkg://fuchsia.com/mod_url#meta/mod_url.cmx";

  auto sessionctl = CreateSessionCtl();
  auto result = RunSessionCtlCommand(sessionctl.get(), {kAddModCommandString, kModUrl});
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(std::string("No package with URL ") + kModUrl + " was found", result.error());
  EXPECT_EQ(0, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddMod_NonFuchsiaPkg) {
  // Add a mod URL starting with something other than fuchsia-pkg:// -- sessionctl
  // has no way of knowing if the mod exists, so it will allow it.
  constexpr char kModUrl[] = "foo://bar";

  auto sessionctl = CreateSessionCtl();
  EXPECT_TRUE(RunSessionCtlCommand(sessionctl.get(), {kAddModCommandString, kModUrl}).is_ok());
  std::string default_name_hash = std::to_string(std::hash<std::string>{}(kModUrl));

  // Assert the story and the mod were added with default story and mod names
  auto story_data = session_storage_->GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ(kModUrl, test_executor_.last_commands().at(0).add_mod().mod_name_transitional);
  EXPECT_EQ(kModUrl, test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModWithoutURL) {
  // Add a mod
  auto sessionctl = CreateSessionCtl();
  EXPECT_TRUE(RunSessionCtlCommand(sessionctl.get(), {kAddModCommandString, "mod_name"}).is_ok());
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
  auto sessionctl = CreateSessionCtl();
  EXPECT_TRUE(
      RunSessionCtlCommand(sessionctl.get(), {kAddModCommandString, "mod_name:0000"}).is_ok());
  std::string default_name_hash = std::to_string(std::hash<std::string>{}("mod_name:0000"));
  auto story_data = session_storage_->GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ("mod_name:0000", test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModBadChars) {
  // Add a mod
  auto sessionctl = CreateSessionCtl();
  EXPECT_TRUE(
      RunSessionCtlCommand(sessionctl.get(), {kAddModCommandString, "a:bad/mod/name"}).is_ok());
  std::string default_name_hash = std::to_string(std::hash<std::string>{}("a:bad/mod/name"));

  auto story_data = session_storage_->GetStoryData(default_name_hash);
  ASSERT_TRUE(story_data);
  EXPECT_EQ(default_name_hash, story_data->story_name());
  EXPECT_EQ("a:bad/mod/name", test_executor_.last_commands().at(0).add_mod().intent.handler);
  EXPECT_EQ(1, test_executor_.execute_count());
}

TEST_F(SessionCtlAppTest, AddModWeb) {
  // Add a mod
  auto sessionctl = CreateSessionCtl();
  EXPECT_TRUE(
      RunSessionCtlCommand(sessionctl.get(), {kAddModCommandString, "https://www.google.com"})
          .is_ok());

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
  auto sessionctl = CreateSessionCtl();
  EXPECT_TRUE(RunSessionCtlCommand(sessionctl.get(), {"--story_name=s", "--mod_name=m",
                                                      kAddModCommandString, "mod_url"})
                  .is_ok());

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
  auto sessionctl = CreateSessionCtl();
  auto command_line = fxl::CommandLineFromInitializerList(
      {kSessionCtlString, "--story_name=??", "--mod_name=m", kAddModCommandString, "mod_url"});
  sessionctl->ExecuteCommand(kAddModCommandString, command_line,
                             [](SessionCtlApp::CommandResult result) {
                               EXPECT_TRUE(result.is_error());
                               EXPECT_EQ("Bad characters in story_name: ??", result.error());
                             });
}

TEST_F(SessionCtlAppTest, AddModMissingModUrl) {
  // Attempt to add a mod without a mod url
  auto sessionctl = CreateSessionCtl();
  auto command_line =
      fxl::CommandLineFromInitializerList({kSessionCtlString, kAddModCommandString});
  sessionctl->ExecuteCommand(
      kAddModCommandString, command_line, [](SessionCtlApp::CommandResult result) {
        EXPECT_TRUE(result.is_error());
        EXPECT_EQ("Missing MOD_URL. Ex: sessionctl add_mod slider_mod", result.error());
      });
}

}  // namespace
}  // namespace modular
