// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <zircon/device/vfs.h>

#include <gmock/gmock.h>
#include <sdk/lib/inspect/testing/cpp/inspect.h>

#include "src/lib/files/glob.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_launcher_component.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_impl.h"

namespace {

using fuchsia::modular::AddMod;
using fuchsia::modular::StoryCommand;
using fuchsia::modular::StoryInfo2;
using testing::HasSubstr;

constexpr char kFakeModuleUrl[] = "fuchsia-pkg://example.com/FAKE_MODULE_PKG/fake_module.cmx";
constexpr char kSessionmgrSelector[] = "*_inspect/sessionmgr.cmx:root";
constexpr char kSessionmgrName[] = "sessionmgr.cmx";
// The initial module's intent parameter data. This needs to be JSON formatted.
constexpr char kIntentAction[] = "action";

class InspectSessionTest : public modular_testing::TestHarnessFixture {
 protected:
  InspectSessionTest()
      : fake_session_shell_(modular_testing::FakeSessionShell::CreateWithDefaultOptions()),
        executor_(dispatcher()) {}

  void RunHarnessAndInterceptSessionShell() {
    fuchsia::modular::testing::TestHarnessSpec spec;
    spec.set_environment_suffix("inspect");
    modular_testing::TestHarnessBuilder builder(std::move(spec));
    builder.InterceptSessionShell(fake_session_shell_->BuildInterceptOptions());
    builder.BuildAndRun(test_harness());

    // Wait for our session shell to start.
    RunLoopUntil([&] { return fake_session_shell_->is_running(); });
  }

  fpromise::result<inspect::contrib::DiagnosticsData> GetInspectDiagnosticsData() {
    auto archive = real_services()->Connect<fuchsia::diagnostics::ArchiveAccessor>();

    inspect::contrib::ArchiveReader reader(std::move(archive), {kSessionmgrSelector});
    fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string> result;
    executor_.schedule_task(
        reader.SnapshotInspectUntilPresent({kSessionmgrName})
            .then([&](fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>&
                          rest) { result = std::move(rest); }));
    RunLoopUntil([&] { return result.is_ok() || result.is_error(); });

    if (result.is_error()) {
      EXPECT_FALSE(result.is_error()) << "Error was " << result.error();
      return fpromise::error();
    }

    if (result.value().size() != 1) {
      EXPECT_EQ(1u, result.value().size()) << "Expected only one component";
      return fpromise::error();
    }

    return fpromise::ok(std::move(result.value()[0]));
  }

  fuchsia::modular::Intent CreateIntent(std::string handler) {
    fuchsia::modular::Intent intent;
    intent.handler = handler;
    intent.action = kIntentAction;

    return intent;
  }

  std::unique_ptr<modular_testing::FakeSessionShell> fake_session_shell_;
  async::Executor executor_;
};  // namespace

TEST_F(InspectSessionTest, NodeHierarchyNoStories) {
  RunHarnessAndInterceptSessionShell();

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  bool called_get_stories = false;
  story_provider->GetStories2(
      nullptr, [&called_get_stories](const std::vector<fuchsia::modular::StoryInfo2>& stories) {
        EXPECT_THAT(stories, testing::IsEmpty());
        called_get_stories = true;
      });

  RunLoopUntil([&] { return called_get_stories; });

  // Check the Inspect node hierarchy is properly set up with only a root.
  auto data_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(data_result.is_ok());
  auto data = data_result.take_value();
  EXPECT_NE(rapidjson::Value(), data.GetByPath({"root"}));
}

TEST_F(InspectSessionTest, DefaultAgentsHierarchy) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.set_environment_suffix("inspect");
  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptSessionShell(fake_session_shell_->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  // Wait for our session shell to start.
  RunLoopUntil([&] { return fake_session_shell_->is_running(); });

  auto data_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(data_result.is_ok());
  auto data = data_result.take_value();
  EXPECT_NE(rapidjson::Value(),
            data.GetByPath({"root", modular_testing::kSessionAgentFakeInterceptionUrl}));
}

TEST_F(InspectSessionTest, CheckNodeHierarchyStartAndStopStory) {
  RunHarnessAndInterceptSessionShell();

  // Create a new story using PuppetMaster and launch a new story shell.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);
  const char kStoryId[] = "my_story";

  puppet_master->ControlStory(kStoryId, story_master.NewRequest());

  auto text_story_annotation_value = fuchsia::modular::AnnotationValue{};
  text_story_annotation_value.set_text("test_value");

  auto text_story_annotation =
      fuchsia::modular::Annotation{.key = "test_key",
                                   .value = std::make_unique<fuchsia::modular::AnnotationValue>(
                                       std::move(text_story_annotation_value))};

  std::vector<fuchsia::modular::Annotation> story_annotations;
  story_annotations.push_back(std::move(text_story_annotation));
  bool done{false};
  story_master->Annotate(std::move(story_annotations),
                         [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                           EXPECT_FALSE(result.is_err());
                           done = true;
                         });
  RunLoopUntil([&] { return done; });

  // Story doesn't start unless it has a mod, so add a mod.
  AddMod add_mod;
  add_mod.mod_name_transitional = "mod1";
  add_mod.intent.handler = kFakeModuleUrl;

  StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  std::vector<StoryCommand> commands;
  commands.push_back(std::move(command));

  story_master->Enqueue(std::move(commands));
  bool execute_called = false;
  story_master->Execute(
      [&execute_called](fuchsia::modular::ExecuteResult result) { execute_called = true; });
  RunLoopUntil([&] { return execute_called; });

  auto data_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(data_result.is_ok());
  auto data = data_result.take_value();
  EXPECT_EQ(rapidjson::Value("test_value"),
            data.GetByPath({"root", kStoryId, "annotation: test_key"}));

  bool story_deleted = false;
  puppet_master->DeleteStory(kStoryId, [&] { story_deleted = true; });

  RunLoopUntil([&] { return story_deleted; });

  // Check that a node is removed from the hierarchy when a story is removed.
  data_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(data_result.is_ok());
  data = data_result.take_value();
  EXPECT_NE(rapidjson::Value(), data.GetByPath({"root"}));
  EXPECT_EQ(rapidjson::Value(),
            data.GetByPath({"root", kStoryId, modular_config::kInspectIsDeleted}));
}

TEST_F(InspectSessionTest, CheckNodeHierarchyMods) {
  RunHarnessAndInterceptSessionShell();

  // Create a new story using PuppetMaster and launch a new story shell.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::StoryPuppetMasterPtr story_master;

  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness()->ConnectToModularService(std::move(svc));

  fuchsia::modular::StoryProvider* story_provider = fake_session_shell_->story_provider();
  ASSERT_TRUE(story_provider != nullptr);
  const char kStoryId[] = "my_story";

  puppet_master->ControlStory(kStoryId, story_master.NewRequest());
  AddMod add_mod;
  add_mod.mod_name_transitional = "mod1";
  auto initial_module_intent = CreateIntent(kFakeModuleUrl);
  add_mod.intent = std::move(initial_module_intent);

  StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  std::vector<StoryCommand> commands;
  commands.push_back(std::move(command));
  story_master->Enqueue(std::move(commands));
  bool execute_called = false;
  story_master->Execute(
      [&execute_called](fuchsia::modular::ExecuteResult result) { execute_called = true; });
  RunLoopUntil([&] { return execute_called; });

  // Annotate the module.
  auto text_mod_annotation_value = fuchsia::modular::AnnotationValue{};
  text_mod_annotation_value.set_bytes({01});
  auto text_mod_annotation =
      fuchsia::modular::Annotation{.key = "text_key",
                                   .value = std::make_unique<fuchsia::modular::AnnotationValue>(
                                       fidl::Clone(text_mod_annotation_value))};
  std::vector<fuchsia::modular::Annotation> mod_annotations;
  mod_annotations.push_back(fidl::Clone(text_mod_annotation));

  auto data_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(data_result.is_ok());
  auto data = data_result.take_value();
  EXPECT_EQ(rapidjson::Value("False"),
            data.GetByPath({"root", kStoryId, kFakeModuleUrl, modular_config::kInspectIsEmbedded}));
  EXPECT_EQ(rapidjson::Value("EXTERNAL"), data.GetByPath({"root", kStoryId, kFakeModuleUrl,
                                                          modular_config::kInspectModuleSource}));
  EXPECT_EQ(rapidjson::Value("action"), data.GetByPath({"root", kStoryId, kFakeModuleUrl,
                                                        modular_config::kInspectIntentAction}));
  EXPECT_EQ(rapidjson::Value("False"),
            data.GetByPath({"root", kStoryId, kFakeModuleUrl, modular_config::kInspectIsDeleted}));
  EXPECT_EQ(rapidjson::Value("NONE"),
            data.GetByPath({"root", kStoryId, kFakeModuleUrl,
                            modular_config::kInspectSurfaceRelationArrangement}));
  EXPECT_EQ(rapidjson::Value("NONE"),
            data.GetByPath({"root", kStoryId, kFakeModuleUrl,
                            modular_config::kInspectSurfaceRelationDependency}));
  EXPECT_EQ(rapidjson::Value(1.0),
            data.GetByPath({"root", kStoryId, kFakeModuleUrl,
                            modular_config::kInspectSurfaceRelationEmphasis}));
  EXPECT_EQ(rapidjson::Value("mod1"),
            data.GetByPath({"root", kStoryId, kFakeModuleUrl, modular_config::kInspectModulePath}));
}

// Tests that sessionmgr exposes its configuration in Inspect.
TEST_F(InspectSessionTest, ExposesConfig) {
  RunHarnessAndInterceptSessionShell();

  auto inspect_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(inspect_result.is_ok());
  auto inspect_data = inspect_result.take_value();

  // The inspect property should contain configuration that uses |session_shell|.
  const auto& config_value = inspect_data.GetByPath({"root", modular_config::kInspectConfig});
  ASSERT_TRUE(config_value.IsString());
  EXPECT_THAT(config_value.GetString(), HasSubstr(fake_session_shell_->url()));
}

// Tests that sessionmgr exposes the configuration provided to it from the session launcher
// component in Inspect.
TEST_F(InspectSessionTest, ExposesConfigFromSessionLauncher) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.set_environment_suffix("inspect");
  modular_testing::TestHarnessBuilder builder(std::move(spec));

  auto session_launcher_component =
      modular_testing::FakeSessionLauncherComponent::CreateWithDefaultOptions();
  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();

  builder.InterceptSessionLauncherComponent(session_launcher_component->BuildInterceptOptions());
  // The session shell is specified in the configuration generated by the session launcher
  // component, so avoid InterceptSessionShell(), which adds it to the configuration in |builder|.
  builder.InterceptComponent(session_shell->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return session_launcher_component->is_running(); });

  EXPECT_TRUE(!session_shell->is_running());

  // Create the configuration that the session launcher component passes to basemgr.
  fuchsia::modular::session::SessionShellMapEntry entry;
  entry.mutable_config()->mutable_app_config()->set_url(session_shell->url());
  fuchsia::modular::session::ModularConfig config;
  config.mutable_basemgr_config()->mutable_session_shell_map()->push_back(std::move(entry));

  fuchsia::mem::Buffer config_buf;
  ASSERT_TRUE(fsl::VmoFromString(modular::ConfigToJsonString(config), &config_buf));

  // Launch the session.
  session_launcher_component->launcher()->LaunchSessionmgr(std::move(config_buf));

  RunLoopUntil([&] { return session_shell->is_running(); });

  auto inspect_result = GetInspectDiagnosticsData();
  ASSERT_TRUE(inspect_result.is_ok());
  auto inspect_data = inspect_result.take_value();

  // The inspect property should contain configuration that uses |session_shell|.
  const auto& config_value = inspect_data.GetByPath({"root", modular_config::kInspectConfig});
  ASSERT_TRUE(config_value.IsString());
  EXPECT_THAT(config_value.GetString(), HasSubstr(session_shell->url()));
}

}  // namespace
