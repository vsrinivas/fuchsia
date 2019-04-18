// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/modular_test_harness/cpp/test_harness_impl.h"

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include "gtest/gtest.h"

constexpr char kFakeBaseShellUrl[] =
    "fuchsia-pkg://example.com/FAKE_BASE_SHELL_PKG/fake_base_shell.cmx";
constexpr char kFakeSessionShellUrl[] =
    "fuchsia-pkg://example.com/FAKE_SESSION_SHELL_PKG/fake_session_shell.cmx";
constexpr char kFakeStoryShellUrl[] =
    "fuchsia-pkg://example.com/FAKE_STORY_SHELL_PKG/fake_story_shell.cmx";
constexpr char kFakeModuleUrl[] =
    "fuchsia-pkg://example.com/FAKE_MODULE_PKG/fake_module.cmx";

namespace modular {
namespace testing {

class TestHarnessImplTest : public sys::testing::TestWithEnvironment {
 public:
  TestHarnessImplTest()
      : harness_impl_(real_env(), harness_.NewRequest(),
                      [this] { did_exit_ = true; }) {}

  fuchsia::modular::testing::TestHarnessPtr& test_harness() {
    return harness_;
  };

  bool did_exit() { return did_exit_; }

  std::vector<std::string> MakeBasemgrArgs(
      fuchsia::modular::testing::TestHarnessSpec spec) {
    return TestHarnessImpl::MakeBasemgrArgs(std::move(spec));
  }

 private:
  bool did_exit_ = false;
  fuchsia::modular::testing::TestHarnessPtr harness_;
  ::modular::testing::TestHarnessImpl harness_impl_;
};

namespace {

TEST_F(TestHarnessImplTest, ExitCallback) {
  test_harness().Unbind();
  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return did_exit(); }, zx::sec(5)));
}

TEST_F(TestHarnessImplTest, DefaultMakeBasemgrArgs) {
  std::vector<std::string> expected = {
      "--test",

      "--base_shell=fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
      "test_base_shell.cmx",

      "--session_shell=fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
      "test_session_shell.cmx",

      "--story_shell=fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
      "test_story_shell.cmx",

      "--sessionmgr_args=--use_memfs_for_ledger,--no_cloud_provider_for_ledger,"
      "--session_agents=fuchsia-pkg://example.com/FAKE_SESSION_AGENT_PKG/"
      "fake_session_agent.cmx"};
  std::vector<std::string> actual =
      MakeBasemgrArgs(fuchsia::modular::testing::TestHarnessSpec{});
  EXPECT_EQ(expected, actual);
}

TEST_F(TestHarnessImplTest, InterceptBaseShell) {
  // Setup base shell interception.
  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  shell_intercept_spec.set_component_url(kFakeBaseShellUrl);
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_base_shell()->set_intercept_spec(
      std::move(shell_intercept_spec));

  // Listen for base shell interception.
  bool intercepted = false;

  test_harness().events().OnNewBaseShell =
      [&intercepted](
          fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) { intercepted = true; };

  test_harness()->Run(std::move(spec));

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return intercepted; }, zx::sec(5)));
};

TEST_F(TestHarnessImplTest, InterceptSessionShell) {
  // Setup session shell interception.
  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  shell_intercept_spec.set_component_url(kFakeSessionShellUrl);
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_session_shell()->set_intercept_spec(
      std::move(shell_intercept_spec));

  // Listen for base shell interception.
  bool intercepted = false;
  test_harness().events().OnNewSessionShell =
      [&intercepted](
          fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) { intercepted = true; };

  test_harness()->Run(std::move(spec));

  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return intercepted; }, zx::sec(5)));
};

TEST_F(TestHarnessImplTest, InterceptStoryShellAndModule) {
  // Setup story shell interception.
  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  shell_intercept_spec.set_component_url(kFakeStoryShellUrl);
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_story_shell()->set_intercept_spec(
      std::move(shell_intercept_spec));

  // Setup kFakeModuleUrl interception.
  {
    fuchsia::modular::testing::InterceptSpec intercept_spec;
    intercept_spec.set_component_url(kFakeModuleUrl);
    spec.mutable_components_to_intercept()->push_back(
        std::move(intercept_spec));
  }

  // Listen for story shell interception.
  bool story_shell_intercepted = false;
  test_harness().events().OnNewStoryShell =
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) { story_shell_intercepted = true; };

  // Listen for module interception.
  bool fake_module_intercepted = false;
  test_harness().events().OnNewComponent =
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) {
        if (startup_info.launch_info.url == kFakeModuleUrl) {
          fake_module_intercepted = true;
        }
      };
  test_harness()->Run(std::move(spec));

  // Create a new story -- this should auto-start the story (because of
  // test_session_shell's behaviour), and launch a new story shell.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::TestHarnessService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->GetService(std::move(svc));

  puppet_master->ControlStory("my_story", story_master.NewRequest());

  using fuchsia::modular::AddMod;
  using fuchsia::modular::StoryCommand;

  std::vector<StoryCommand> cmds;
  StoryCommand cmd;
  AddMod add_mod;
  add_mod.mod_name = {"mod_name"};
  add_mod.intent.handler = kFakeModuleUrl;
  add_mod.surface_relation = fuchsia::modular::SurfaceRelation{};
  cmd.set_add_mod(std::move(add_mod));
  cmds.push_back(std::move(cmd));

  story_master->Enqueue(std::move(cmds));
  story_master->Execute([](fuchsia::modular::ExecuteResult result) {});

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return story_shell_intercepted; },
                                        zx::sec(10)));
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return fake_module_intercepted; },
                                        zx::sec(10)));
};

}  // namespace
}  // namespace testing
}  // namespace modular
