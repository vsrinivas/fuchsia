// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/modular_test_harness/cpp/fake_component.h>
#include <lib/modular_test_harness/cpp/fake_module.h>
#include <lib/modular_test_harness/cpp/fake_session_shell.h>
#include <lib/modular_test_harness/cpp/fake_story_shell.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <sdk/lib/sys/cpp/component_context.h>
#include <sdk/lib/sys/cpp/service_directory.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>
#include <src/lib/fxl/logging.h>

#include "peridot/lib/testing/session_shell_impl.h"

namespace {

constexpr char kParentModuleName[] = "parent_name";
constexpr char kEmbeddedModuleName[] = "embedded_name";
constexpr char kThirdModuleName[] = "third_name";
constexpr char kStoryName[] = "story";

class StoryShellEmbeddedModTest : public modular::testing::TestHarnessFixture {
 public:
  void SetUp() override {
    // Listen for session shell interception
    builder_.InterceptSessionShell(
        fake_session_shell_.GetOnCreateHandler(),
        {.sandbox_services = {"fuchsia.modular.SessionShellContext",
                              "fuchsia.modular.PuppetMaster"}});

    // Listen for story shell interception
    builder_.InterceptStoryShell(
        test_story_shell_.GetOnCreateHandler(),
        {.sandbox_services = {"fuchsia.modular.StoryShellContext"}});

    // Listen for parent module interception
    parent_module_ = std::make_unique<modular::testing::FakeModule>(
        [this](fuchsia::modular::Intent intent) {});
    parent_module_url_ = builder_.GenerateFakeUrl();
    builder_.InterceptComponent(
        parent_module_->GetOnCreateHandler(),
        {.url = parent_module_url_,
         .sandbox_services =
             modular::testing::FakeModule::GetSandboxServices()});

    // Listen for embedded module interception
    embedded_module_ = std::make_unique<modular::testing::FakeModule>(
        [this](fuchsia::modular::Intent intent) {});
    embedded_module_url_ = builder_.GenerateFakeUrl();
    builder_.InterceptComponent(
        embedded_module_->GetOnCreateHandler(),
        {.url = embedded_module_url_,
         .sandbox_services =
             modular::testing::FakeModule::GetSandboxServices()});

    // Listen for third module interception
    third_module_ = std::make_unique<modular::testing::FakeModule>(
        [this](fuchsia::modular::Intent intent) {});
    third_module_url_ = builder_.GenerateFakeUrl();
    builder_.InterceptComponent(
        third_module_->GetOnCreateHandler(),
        {.url = third_module_url_,
         .sandbox_services =
             modular::testing::FakeModule::GetSandboxServices()});

    // Start modular
    test_harness().events().OnNewComponent =
        builder_.BuildOnNewComponentHandler();
    test_harness()->Run(builder_.BuildSpec());

    // Wait for session shell to start
    RunLoopUntil([&] { return fake_session_shell_.is_running(); });
  }

  // Launches an initial parent module.
  void LaunchParentModule() {
    auto parent_mod_intent =
        fuchsia::modular::Intent{.handler = parent_module_url_};
    AddModToStory(std::move(parent_mod_intent), kParentModuleName, kStoryName);

    RunLoopUntil([&] { return parent_module_->is_running(); });
  }

  // The parent module embeds a module.
  void ParentModuleEmbedsModule() {
    auto embedded_mod_intent =
        fuchsia::modular::Intent{.handler = embedded_module_url_};
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    fuchsia::modular::ModuleControllerPtr module_controller;
    parent_module_->module_context()->EmbedModule2(
        kEmbeddedModuleName, std::move(embedded_mod_intent),
        module_controller.NewRequest(), std::move(view_token),
        [](const fuchsia::modular::StartModuleStatus status) {});

    RunLoopUntil([&] { return embedded_module_->is_running(); });
  }

  // The embedded module launches a third module in the story shell.
  // In this case, the story shell doesn't know about the direct parent of
  // the third module because it's embedded and its view is not sent to the
  // story shell. Instead, module one must be used as the display parent module
  // declared to the story shell for the view of module three.
  void EmbeddedModuleLaunchesModule() {
    fuchsia::modular::ModuleControllerPtr third_module_ptr;
    auto third_module_intent =
        fuchsia::modular::Intent{.handler = third_module_url_};
    embedded_module_->module_context()->AddModuleToStory(
        kThirdModuleName, std::move(third_module_intent),
        third_module_ptr.NewRequest(), /* surface_relation */ nullptr,
        [](const fuchsia::modular::StartModuleStatus) {});

    RunLoopUntil([&] { return third_module_->is_running(); });
  }

  modular::testing::FakeSessionShell fake_session_shell_;
  modular::testing::FakeStoryShell test_story_shell_;
  std::unique_ptr<modular::testing::FakeModule> parent_module_;
  std::unique_ptr<modular::testing::FakeModule> embedded_module_;
  std::unique_ptr<modular::testing::FakeModule> third_module_;
  modular::testing::TestHarnessBuilder builder_;
  std::string parent_module_url_;
  std::string embedded_module_url_;
  std::string third_module_url_;
};

}  // namespace

// Checks the surface relationships between three modules.
TEST_F(StoryShellEmbeddedModTest, SurfaceRelationships) {
  // Set expectations for AddSurface(). Note that we only expect story shell to
  // add a surface for non-embedded modules.
  test_story_shell_.set_on_add_surface(
      [&](fuchsia::modular::ViewConnection view_connection,
          fuchsia::modular::SurfaceInfo surface_info) {
        // Continue if this is adding the parent module
        if (view_connection.surface_id == kParentModuleName) {
          return;
        }

        // These should pass when the third module is added to the story
        EXPECT_EQ(view_connection.surface_id,
                  "parent_name:embedded_name:third_name");
        EXPECT_EQ(surface_info.parent_id, kParentModuleName);
      });

  LaunchParentModule();

  // Have the parent module launch an embedded module
  ParentModuleEmbedsModule();

  // Have the embedded module launch a third module
  EmbeddedModuleLaunchesModule();
}

// Checks that embedded modules are not reinflated when stories are retarted.
TEST_F(StoryShellEmbeddedModTest, DISABLED_ReinflateModules) {
  LaunchParentModule();
  ParentModuleEmbedsModule();
  EmbeddedModuleLaunchesModule();

  fuchsia::modular::StoryControllerPtr story_controller;
  fake_session_shell_.story_provider()->GetController(
      kStoryName, story_controller.NewRequest());
  bool modules_reinflated_correctly{false};

  // Stop and restart the story
  story_controller->Stop([&] {
    story_controller->RequestStart();
    story_controller->GetActiveModules(
        [&](std::vector<fuchsia::modular::ModuleData> active_modules) mutable {
          size_t num_embedded_mods = 0u;
          for (const fuchsia::modular::ModuleData& mod : active_modules) {
            num_embedded_mods += mod.is_embedded;
          }
          if (num_embedded_mods == 0 && active_modules.size() == 2u) {
            modules_reinflated_correctly = true;
          }
        });
  });

  RunLoopUntil([&] { return modules_reinflated_correctly; });
}
