// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/inspect/testing/cpp/inspect.h>
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
using inspect::testing::DoubleIs;
using inspect::testing::IntIs;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using testing::IsNull;
using testing::Not;

constexpr char kFakeModuleUrl[] = "fuchsia-pkg://example.com/FAKE_MODULE_PKG/fake_module.cmx";
constexpr char kSessionmgrInspectRootGlobPath[] =
    "/hub/r/mth_*_inspect/*/c/sessionmgr.cmx/*/out/inspect/root.inspect";
// The initial module's intent parameter data. This needs to be JSON formatted.
constexpr char kInitialIntentParameterData[] = "\"initial\"";
constexpr char kIntentAction[] = "action";
constexpr char kIntentParameterName[] = "intent_parameter";

class InspectSessionTest : public modular_testing::TestHarnessFixture {
 protected:
  InspectSessionTest()
      : fake_session_shell_(modular_testing::FakeSessionShell::CreateWithDefaultOptions()) {}

  void RunHarnessAndInterceptSessionShell() {
    fuchsia::modular::testing::TestHarnessSpec spec;
    spec.set_environment_suffix("inspect");
    modular_testing::TestHarnessBuilder builder(std::move(spec));
    builder.InterceptSessionShell(fake_session_shell_->BuildInterceptOptions());
    builder.BuildAndRun(test_harness());

    // Wait for our session shell to start.
    RunLoopUntil([&] { return fake_session_shell_->is_running(); });
  }

  zx_status_t GetInspectVmo(zx::vmo* out_vmo) {
    files::Glob glob(kSessionmgrInspectRootGlobPath);
    if (glob.size() == 0) {
      return ZX_ERR_NOT_FOUND;
    }

    fuchsia::io::FileSyncPtr file;
    zx_status_t status;
    status = fdio_open(std::string(*glob.begin()).c_str(), ZX_FS_RIGHT_READABLE,
                       file.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      return status;
    }

    EXPECT_TRUE(file.is_bound());

    fuchsia::io::NodeInfo info;
    auto get_status = file->Describe(&info);
    if (get_status != ZX_OK) {
      printf("get failed\n");
      return get_status;
    }

    if (!info.is_vmofile()) {
      printf("not a vmofile");
      return ZX_ERR_NOT_FOUND;
    }

    *out_vmo = std::move(info.vmofile().vmo);
    return ZX_OK;
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

  // Check the iquery node hierarchy is properly set up with only a root.
  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, GetInspectVmo(&vmo));
  auto hierarchy = inspect::ReadFromVmo(std::move(vmo)).take_value();

  EXPECT_THAT(hierarchy,
              AllOf(inspect::testing::NodeMatches(inspect::testing::NameMatches("root"))));
}

TEST_F(InspectSessionTest, DefaultAgentsHierarchy) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.set_environment_suffix("inspect");
  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptSessionShell(fake_session_shell_->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  // Wait for our session shell to start.
  RunLoopUntil([&] { return fake_session_shell_->is_running(); });

  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, GetInspectVmo(&vmo));
  auto hierarchy = inspect::ReadFromVmo(std::move(vmo)).take_value();
  EXPECT_THAT(hierarchy, (NodeMatches(NameMatches("root"))));

  const auto& child = hierarchy.children();
  ASSERT_THAT(child.size(), 1);
  EXPECT_THAT(child.at(0),
              NodeMatches(NameMatches(modular_testing::kSessionAgentFakeInterceptionUrl)));
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

  zx::vmo vmo_inspect;
  ASSERT_EQ(ZX_OK, GetInspectVmo(&vmo_inspect));
  auto hierarchy = inspect::ReadFromVmo(std::move(vmo_inspect)).take_value();
  EXPECT_THAT(hierarchy, (NodeMatches(NameMatches("root"))));

  const auto& children = hierarchy.children();
  // Contains 1 agent and 1 story
  ASSERT_THAT(children.size(), 2);
  EXPECT_THAT(children, Contains(NodeMatches(NameMatches("my_story"))));
  EXPECT_THAT(children.at(0), NodeMatches(AllOf(PropertyList(UnorderedElementsAre(
                                  IntIs("last_focus_time", last_focus_timestamps.back()),
                                  StringIs("annotation: test_key", "test_value"))))));

  bool story_deleted = false;
  puppet_master->DeleteStory(kStoryId, [&] { story_deleted = true; });

  RunLoopUntil([&] { return story_deleted; });

  // Check that a node is removed from the hierarchy when a story is removed.
  ASSERT_EQ(ZX_OK, GetInspectVmo(&vmo_inspect));

  hierarchy = inspect::ReadFromVmo(std::move(vmo_inspect)).take_value();
  EXPECT_THAT(hierarchy, AllOf(NodeMatches(NameMatches("root"))));
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

  zx::vmo vmo_inspect;
  ASSERT_EQ(ZX_OK, GetInspectVmo(&vmo_inspect));
  auto hierarchy = inspect::ReadFromVmo(std::move(vmo_inspect)).take_value();
  EXPECT_THAT(hierarchy, (NodeMatches(NameMatches("root"))));

  const auto& child = hierarchy.children();
  ASSERT_THAT(child.size(), 2);

  EXPECT_THAT(child.at(0), NodeMatches(NameMatches("my_story")));

  const auto& grandchild = child.at(0).children();

  ASSERT_THAT(grandchild.size(), 1);
  EXPECT_THAT(grandchild.at(0), NodeMatches(NameMatches(kFakeModuleUrl)));
  EXPECT_THAT(grandchild.at(0),
              NodeMatches(AllOf(PropertyList(UnorderedElementsAre(
                  StringIs(modular_config::kInspectIsEmbedded, "False"),
                  StringIs(modular_config::kInspectModuleSource, "EXTERNAL"),
                  StringIs(modular_config::kInspectIntentHandler, kFakeModuleUrl),
                  StringIs(modular_config::kInspectIntentAction, "action"),
                  StringIs(modular_config::kInspectIsDeleted, "False"),
                  StringIs(modular_config::kInspectIntentParams, "name : intent_parameter "),
                  StringIs(modular_config::kInspectSurfaceRelationArrangement, "NONE"),
                  StringIs(modular_config::kInspectSurfaceRelationDependency, "NONE"),
                  DoubleIs(modular_config::kInspectSurfaceRelationEmphasis, 1.0),
                  StringIs(modular_config::kInspectModulePath, "mod1"),
                  StringIs("annotation: text_key", "bytes"))))));
}
}  // namespace
