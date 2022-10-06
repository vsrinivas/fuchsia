// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/test_harness_impl.h"

#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/vfs/cpp/pseudo_dir.h>

#include <thread>

#include <gtest/gtest.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>
#include <src/modular/lib/modular_config/modular_config.h>
#include <src/modular/lib/modular_config/modular_config_constants.h>
#include <src/modular/lib/pseudo_dir/pseudo_dir_server.h>

#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/substitute.h"

constexpr char kFakeSessionShellUrl[] =
    "fuchsia-pkg://example.com/FAKE_SESSION_SHELL_PKG/fake_session_shell.cmx";
constexpr char kFakeStoryShellUrl[] =
    "fuchsia-pkg://example.com/FAKE_STORY_SHELL_PKG/fake_story_shell.cmx";
constexpr char kFakeModuleUrl[] = "fuchsia-pkg://example.com/FAKE_MODULE_PKG/fake_module.cmx";

namespace modular_testing {
namespace {
std::string GenerateFakeUrl() {
  uint32_t random_number = 0;
  zx_cprng_draw(&random_number, sizeof random_number);
  constexpr char kFakeUrlSubstitutePattern[] =
      "fuchsia-pkg://example.com/GENERATED_URL_$0#meta/GENERATED_URL_$0.cmx";
  return fxl::Substitute(kFakeUrlSubstitutePattern, std::to_string(random_number));
}
}  // namespace

class TestHarnessImplTest : public gtest::TestWithEnvironmentFixture {
 public:
  TestHarnessImplTest() : harness_impl_(real_env(), [this] { did_exit_ = true; }) {
    harness_impl_.Bind(harness_.NewRequest());
  }

  fuchsia::modular::testing::TestHarnessPtr& test_harness() { return harness_; }

  bool did_exit() { return did_exit_; }

  std::unique_ptr<vfs::PseudoDir> MakeBasemgrConfigDir(
      fuchsia::modular::testing::TestHarnessSpec spec) {
    return TestHarnessImpl::MakeBasemgrConfigDir(std::move(spec));
  }

 private:
  bool did_exit_ = false;
  fuchsia::modular::testing::TestHarnessPtr harness_;
  ::modular_testing::TestHarnessImpl harness_impl_;
};

namespace {

// Check that the config that TestHarnessImpl generates is readable by
// ModuleConfigReader.
TEST_F(TestHarnessImplTest, MakeBasemgrConfigDir) {
  constexpr char kSessionShellForTest[] =
      "fuchsia-pkg://example.com/TestHarnessImplTest#meta/"
      "TestHarnessImplTest.cmx";

  fuchsia::modular::testing::TestHarnessSpec spec;
  fuchsia::modular::session::SessionShellMapEntry session_shell_entry;
  session_shell_entry.mutable_config()->mutable_app_config()->set_url(kSessionShellForTest);

  spec.mutable_basemgr_config()->mutable_session_shell_map()->push_back(
      std::move(session_shell_entry));

  // Construct "/config_override/data" dirs, and add MakeBasemgrConfigDir() to
  // "data" dir.
  auto namespace_dir = std::make_unique<vfs::PseudoDir>();
  {
    auto dir_split = fxl::SplitString(modular_config::kOverriddenConfigDir, "/",
                                      fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
    ASSERT_EQ(2u, dir_split.size());

    auto second_dir = std::make_unique<vfs::PseudoDir>();
    second_dir->AddEntry(std::string(dir_split[1]), MakeBasemgrConfigDir(std::move(spec)));
    namespace_dir->AddEntry(std::string(dir_split[0]), std::move(second_dir));
  }

  modular::PseudoDirServer server(std::move(namespace_dir));
  modular::ModularConfigReader config_reader(server.OpenAt("."));
  EXPECT_EQ(kSessionShellForTest,
            config_reader.GetBasemgrConfig().session_shell_map().at(0).config().app_config().url());
}

// Test that additional injected services are made available, spin up the
// associated component when requested. This test exercises injecting a custom
// service.
TEST_F(TestHarnessImplTest, ComponentProvidedService) {
  fuchsia::modular::testing::TestHarnessSpec spec;

  auto generated_componentctx_url = GenerateFakeUrl();

  spec.mutable_env_services()->mutable_services_from_components()->push_back(
      fuchsia::modular::testing::ComponentService{// Provide a custom injected service.
                                                  .name = fuchsia::modular::ComponentContext::Name_,
                                                  .url = generated_componentctx_url});

  // Intercept the component URL which supplies ComponentContext.
  {
    fuchsia::modular::testing::InterceptSpec intercept_spec;
    intercept_spec.set_component_url(generated_componentctx_url);
    spec.mutable_components_to_intercept()->push_back(std::move(intercept_spec));
  }

  bool intercepted_componentctx = false;
  test_harness().events().OnNewComponent =
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
        if (startup_info.launch_info.url == generated_componentctx_url) {
          intercepted_componentctx = true;
        } else {
          ASSERT_FALSE("Started for no reason.");
        }
      };

  test_harness()->Run(std::move(spec));

  fuchsia::modular::ComponentContextPtr componentctx;
  test_harness()->ConnectToEnvironmentService(fuchsia::modular::ComponentContext::Name_,
                                              componentctx.NewRequest().TakeChannel());

  RunLoopUntil([&] { return intercepted_componentctx; });
}

TEST_F(TestHarnessImplTest, InterceptSessionShell) {
  fuchsia::modular::testing::TestHarnessSpec spec;

  // 1. Setup session shell interception.
  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  shell_intercept_spec.set_component_url(kFakeSessionShellUrl);
  {
    fuchsia::modular::session::SessionShellMapEntry entry;
    entry.mutable_config()->mutable_app_config()->set_url(kFakeSessionShellUrl);

    spec.mutable_basemgr_config()->mutable_session_shell_map()->push_back(std::move(entry));
  }
  spec.mutable_components_to_intercept()->push_back(std::move(shell_intercept_spec));

  // 2. Listen for session shell interception.
  bool intercepted = false;
  test_harness().events().OnNewComponent =
      [&intercepted](
          fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
        if (startup_info.launch_info.url == kFakeSessionShellUrl)
          intercepted = true;
      };

  test_harness()->Run(std::move(spec));

  RunLoopUntil([&] { return intercepted; });
}

TEST_F(TestHarnessImplTest, InterceptStoryShellAndModule) {
  // Setup story shell interception.
  fuchsia::modular::testing::InterceptSpec shell_intercept_spec;
  shell_intercept_spec.set_component_url(kFakeStoryShellUrl);

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_basemgr_config()->mutable_story_shell()->mutable_app_config()->set_url(
      shell_intercept_spec.component_url());
  spec.mutable_components_to_intercept()->push_back(std::move(shell_intercept_spec));

  // Setup kFakeModuleUrl interception.
  {
    fuchsia::modular::testing::InterceptSpec intercept_spec;
    intercept_spec.set_component_url(kFakeModuleUrl);
    spec.mutable_components_to_intercept()->push_back(std::move(intercept_spec));
  }

  // Listen for story shell interception.
  bool story_shell_intercepted = false;
  // Listen for module interception.
  bool fake_module_intercepted = false;

  test_harness().events().OnNewComponent =
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
        if (startup_info.launch_info.url == kFakeModuleUrl) {
          fake_module_intercepted = true;
        } else if (startup_info.launch_info.url == kFakeStoryShellUrl) {
          story_shell_intercepted = true;
        }
      };
  test_harness()->Run(std::move(spec));

  // Create a new story -- this should auto-start the story (because of
  // test_session_shell's behaviour), and launch a new story shell.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

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

  RunLoopUntil([&] { return story_shell_intercepted; });
  RunLoopUntil([&] { return fake_module_intercepted; });
}

// Tests that services in |TestHarnessSpec.env_services.service_dir| are
// accessible in the test harness environment.
TEST_F(TestHarnessImplTest, EnvironmentServiceDirectory) {
  constexpr char kTestServiceName[] = "my.test.service";

  bool svc_requested = false;
  auto svc_dir = std::make_unique<vfs::PseudoDir>();
  svc_dir->AddEntry(kTestServiceName,
                    std::make_unique<vfs::Service>(
                        [&svc_requested](zx::channel request, async_dispatcher_t* dispatcher) {
                          svc_requested = true;
                        }));

  modular::PseudoDirServer svc_dir_server(std::move(svc_dir));

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_env_services()->set_service_dir(svc_dir_server.Serve().Unbind().TakeChannel());
  test_harness()->Run(std::move(spec));

  fuchsia::io::NodePtr node;
  test_harness()->ConnectToEnvironmentService(kTestServiceName, node.NewRequest().TakeChannel());
  RunLoopUntil([&] { return svc_requested; });
}

// Tests that that the test harness correctly parses modular configs from a
// string.
TEST_F(TestHarnessImplTest, ParseConfigFromString) {
  auto config = R"({
  "basemgr": {
    "session_shells": [
      {
        "url": "fuchsia-pkg://fuchsia.com/test_session_shell#meta/test_session_shell.cmx"
      }
    ]
  },
  "sessionmgr": {
    "startup_agents": [
      "fuchsia-pkg://fuchsia.com/startup_agent#meta/startup_agent.cmx"
    ]
  }
  })";

  fuchsia::modular::session::BasemgrConfig basemgr_config;
  fuchsia::modular::session::SessionmgrConfig sessionmgr_config;
  bool done = false;
  test_harness()->ParseConfig(
      config, [&](fuchsia::modular::session::BasemgrConfig parsed_basemgr_config,
                  fuchsia::modular::session::SessionmgrConfig parsed_sessionmgr_config) {
        basemgr_config = std::move(parsed_basemgr_config);
        sessionmgr_config = std::move(parsed_sessionmgr_config);
        done = true;
      });

  RunLoopUntil([&] { return done; });

  ASSERT_EQ(1u, basemgr_config.session_shell_map().size());
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/test_session_shell#meta/test_session_shell.cmx",
            basemgr_config.session_shell_map().at(0).config().app_config().url());
}

}  // namespace
}  // namespace modular_testing
