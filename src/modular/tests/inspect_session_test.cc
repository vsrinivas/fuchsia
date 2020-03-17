// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <zircon/device/vfs.h>

#include <sdk/lib/inspect/testing/cpp/inspect.h>

#include "gmock/gmock.h"
#include "src/lib/files/glob.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_impl.h"

namespace {

using fuchsia::modular::AddMod;
using fuchsia::modular::StoryCommand;
using fuchsia::modular::StoryInfo;
using fuchsia::modular::StoryInfo2;
using fuchsia::modular::StoryState;
using fuchsia::modular::StoryVisibilityState;
using fuchsia::modular::ViewIdentifier;
using ::fxl::Substitute;
using testing::IsNull;
using testing::Not;

constexpr char kFakeModuleUrl[] = "fuchsia-pkg://example.com/FAKE_MODULE_PKG/fake_module.cmx";
constexpr char kSessionmgrSelector[] = "*_inspect/sessionmgr.cmx:root";
constexpr char kSessionmgrName[] = "sessionmgr.cmx";
// The initial module's intent parameter data. This needs to be JSON formatted.
constexpr char kInitialIntentParameterData[] = "\"initial\"";
constexpr char kIntentAction[] = "action";
constexpr char kIntentParameterName[] = "intent_parameter";

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

  fit::result<inspect::contrib::DiagnosticsData> GetInspect() {
    auto archive = real_services()->Connect<fuchsia::diagnostics::Archive>();

    inspect::contrib::ArchiveReader reader(std::move(archive), {kSessionmgrSelector});
    fit::result<std::vector<inspect::contrib::DiagnosticsData>, std::string> result;
    executor_.schedule_task(
        reader.SnapshotInspectUntilPresent({kSessionmgrName})
            .then([&](fit::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>&
                          rest) { result = std::move(rest); }));
    RunLoopUntil([&] { return result.is_ok() || result.is_error(); });

    if (result.is_error()) {
      EXPECT_FALSE(result.is_error()) << "Error was " << result.error();
      return fit::error();
    }

    if (result.value().size() != 1) {
      EXPECT_EQ(1u, result.value().size()) << "Expected only one component";
      return fit::error();
    }

    return fit::ok(std::move(result.value()[0]));
  }

  fuchsia::modular::Intent CreateIntent(std::string handler, std::string parameter_name,
                                        std::string parameter_data) {
    fuchsia::modular::Intent intent;
    intent.handler = handler;
    intent.action = kIntentAction;

    fuchsia::modular::IntentParameter intent_parameter;
    intent_parameter.name = parameter_name;
    intent_parameter.data = fuchsia::modular::IntentParameterData();
    fsl::SizedVmo vmo;
    ZX_ASSERT(fsl::VmoFromString(parameter_data, &vmo));
    intent_parameter.data.set_json(std::move(vmo).ToTransport());
    intent.parameters.emplace();
    intent.parameters->push_back(std::move(intent_parameter));

    return intent;
  }

  std::unique_ptr<modular_testing::FakeSessionShell> fake_session_shell_;
  async::Executor executor_;
};  // namespace

class TestStoryProviderWatcher : public fuchsia::modular::StoryProviderWatcher {
 public:
  TestStoryProviderWatcher() : binding_(this) {}
  ~TestStoryProviderWatcher() override = default;

  void OnChange2(fit::function<void(fuchsia::modular::StoryInfo2)> on_change) {
    on_change_2_ = std::move(on_change);
  }

  void Watch(fuchsia::modular::StoryProvider* const story_provider) {
    story_provider->Watch(binding_.NewBinding());
  }

 private:
  // |fuchsia::modular::StoryProviderWatcher|
  void OnDelete(::std::string story_id) override {}

  // |fuchsia::modular::StoryProviderWatcher|
  void OnChange2(fuchsia::modular::StoryInfo2 story_info, fuchsia::modular::StoryState story_state,
                 fuchsia::modular::StoryVisibilityState story_visibility_state) override {
    on_change_2_(std::move(story_info));
    return;
  }

  fit::function<void(fuchsia::modular::StoryInfo2)> on_change_2_;
  fidl::Binding<fuchsia::modular::StoryProviderWatcher> binding_;
};

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
  auto data_result = GetInspect();
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

  auto data_result = GetInspect();
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

  auto text_story_annotation = fuchsia::modular::Annotation{
      .key = "test_key", .value = fidl::MakeOptional(std::move(text_story_annotation_value))};

  std::vector<fuchsia::modular::Annotation> story_annotations;
  story_annotations.push_back(std::move(text_story_annotation));
  bool done{false};
  story_master->Annotate(std::move(story_annotations),
                         [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                           EXPECT_FALSE(result.is_err());
                           done = true;
                         });
  RunLoopUntil([&] { return done; });

  // Watch for changes to the session.
  TestStoryProviderWatcher story_provider_watcher;
  story_provider_watcher.Watch(fake_session_shell_->story_provider());

  // Keep track of the focus timestamp that we receive for the story.
  std::vector<int64_t> last_focus_timestamps;
  story_provider_watcher.OnChange2([&](fuchsia::modular::StoryInfo2 story_info) {
    ASSERT_TRUE(story_info.has_id());
    ASSERT_TRUE(story_info.has_last_focus_time());
    ASSERT_EQ(kStoryId, story_info.id());
    last_focus_timestamps.push_back(story_info.last_focus_time());
  });

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

  auto data_result = GetInspect();
  ASSERT_TRUE(data_result.is_ok());
  auto data = data_result.take_value();
  EXPECT_EQ(rapidjson::Value(last_focus_timestamps.back()),
            data.GetByPath({"root", kStoryId, "last_focus_time"}));
  EXPECT_EQ(rapidjson::Value("test_value"),
            data.GetByPath({"root", kStoryId, "annotation: test_key"}));

  bool story_deleted = false;
  puppet_master->DeleteStory(kStoryId, [&] { story_deleted = true; });

  RunLoopUntil([&] { return story_deleted; });

  // Check that a node is removed from the hierarchy when a story is removed.
  // TODO(fxb/48109): This test must check that root/my_story is missing, but it is actually
  // present. Update this test when the underlying bug is fixed.
  data_result = GetInspect();
  ASSERT_TRUE(data_result.is_ok());
  data = data_result.take_value();
  EXPECT_NE(rapidjson::Value(), data.GetByPath({"root"}));
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
  auto initial_module_intent =
      CreateIntent(kFakeModuleUrl, kIntentParameterName, kInitialIntentParameterData);
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
  auto text_mod_annotation = fuchsia::modular::Annotation{
      .key = "text_key", .value = fidl::MakeOptional(fidl::Clone(text_mod_annotation_value))};
  std::vector<fuchsia::modular::Annotation> mod_annotations;
  mod_annotations.push_back(fidl::Clone(text_mod_annotation));

  bool annotate_done = false;
  story_master->AnnotateModule(
      "mod1", std::move(mod_annotations),
      [&](fuchsia::modular::StoryPuppetMaster_AnnotateModule_Result result) {
        EXPECT_FALSE(result.is_err());
        annotate_done = true;
      });
  RunLoopUntil([&] { return annotate_done && execute_called; });

  auto data_result = GetInspect();
  ASSERT_TRUE(data_result.is_ok());
  auto data = data_result.take_value();
  EXPECT_EQ(rapidjson::Value("False"),
            data.GetByPath({"root", kStoryId, kFakeModuleUrl, modular_config::kInspectIsEmbedded}));
  EXPECT_EQ(rapidjson::Value("EXTERNAL"), data.GetByPath({"root", kStoryId, kFakeModuleUrl,
                                                          modular_config::kInspectModuleSource}));
  EXPECT_EQ(
      rapidjson::Value(kFakeModuleUrl),
      data.GetByPath({"root", kStoryId, kFakeModuleUrl, modular_config::kInspectIntentHandler}));
  EXPECT_EQ(rapidjson::Value("action"), data.GetByPath({"root", kStoryId, kFakeModuleUrl,
                                                        modular_config::kInspectIntentAction}));
  EXPECT_EQ(rapidjson::Value("False"),
            data.GetByPath({"root", kStoryId, kFakeModuleUrl, modular_config::kInspectIsDeleted}));
  EXPECT_EQ(
      rapidjson::Value("name : intent_parameter "),
      data.GetByPath({"root", kStoryId, kFakeModuleUrl, modular_config::kInspectIntentParams}));
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
  EXPECT_EQ(rapidjson::Value("bytes"),
            data.GetByPath({"root", kStoryId, kFakeModuleUrl, "annotation: text_key"}));
}
}  // namespace
