// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_story_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

constexpr char kParentModuleName[] = "parent_name";
constexpr char kEmbeddedModuleName[] = "embedded_name";
constexpr char kThirdModuleName[] = "third_name";
constexpr char kStoryName[] = "story";

class StoryShellEmbeddedModTest : public modular_testing::TestHarnessFixture {
 public:
  StoryShellEmbeddedModTest()
      : fake_session_shell_(modular_testing::FakeSessionShell::CreateWithDefaultOptions()),
        test_story_shell_({.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
                           .sandbox_services = {"fuchsia.modular.StoryShellContext"}}),
        parent_module_(modular_testing::FakeModule::CreateWithDefaultOptions()),
        embedded_module_(modular_testing::FakeModule::CreateWithDefaultOptions()),
        third_module_(modular_testing::FakeModule::CreateWithDefaultOptions()) {
    builder_.InterceptSessionShell(fake_session_shell_->BuildInterceptOptions());
    builder_.InterceptStoryShell(test_story_shell_.BuildInterceptOptions());
    builder_.InterceptComponent(parent_module_->BuildInterceptOptions());
    builder_.InterceptComponent(embedded_module_->BuildInterceptOptions());
    builder_.InterceptComponent(third_module_->BuildInterceptOptions());

    // Start modular
    builder_.BuildAndRun(test_harness());

    // Wait for session shell to start
    RunLoopUntil([&] { return fake_session_shell_->is_running(); });
  }

  // Launches an initial parent module.
  void LaunchParentModule() {
    auto parent_mod_intent = fuchsia::modular::Intent{.handler = parent_module_->url()};
    modular_testing::AddModToStory(test_harness(), kStoryName, kParentModuleName,
                                   std::move(parent_mod_intent));

    RunLoopUntil([&] { return parent_module_->is_running(); });
  }

  // The parent module embeds a module.
  void ParentModuleEmbedsModule() {
    auto embedded_mod_intent = fuchsia::modular::Intent{.handler = embedded_module_->url()};
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    fuchsia::modular::ModuleControllerPtr module_controller;
    parent_module_->module_context()->EmbedModule(
        kEmbeddedModuleName, std::move(embedded_mod_intent), module_controller.NewRequest(),
        std::move(view_token), [](const fuchsia::modular::StartModuleStatus status) {});

    RunLoopUntil([&] { return embedded_module_->is_running(); });
  }

  // The embedded module launches a third module in the story shell.
  // In this case, the story shell doesn't know about the direct parent of
  // the third module because it's embedded and its view is not sent to the
  // story shell. Instead, module one must be used as the display parent module
  // declared to the story shell for the view of module three.
  void EmbeddedModuleLaunchesModule() {
    fuchsia::modular::ModuleControllerPtr third_module_ptr;
    auto third_module_intent = fuchsia::modular::Intent{.handler = third_module_->url()};
    embedded_module_->module_context()->AddModuleToStory(
        kThirdModuleName, std::move(third_module_intent), third_module_ptr.NewRequest(),
        /* surface_relation */ nullptr, [](const fuchsia::modular::StartModuleStatus) {});

    RunLoopUntil([&] { return third_module_->is_running(); });
  }

  std::unique_ptr<modular_testing::FakeSessionShell> fake_session_shell_;
  modular_testing::FakeStoryShell test_story_shell_;
  std::unique_ptr<modular_testing::FakeModule> parent_module_;
  std::unique_ptr<modular_testing::FakeModule> embedded_module_;
  std::unique_ptr<modular_testing::FakeModule> third_module_;
  modular_testing::TestHarnessBuilder builder_;
};

}  // namespace

// Checks the surface relationships between three modules.
TEST_F(StoryShellEmbeddedModTest, SurfaceRelationships) {
  // Set expectations for AddSurface(). Note that we only expect story shell to
  // add a surface for non-embedded modules.
  test_story_shell_.set_on_add_surface([&](fuchsia::modular::ViewConnection view_connection,
                                           fuchsia::modular::SurfaceInfo surface_info) {
    // Continue if this is adding the parent module
    if (view_connection.surface_id == kParentModuleName) {
      return;
    }

    // These should pass when the third module is added to the story
    EXPECT_EQ(view_connection.surface_id, "parent_name:embedded_name:third_name");
    EXPECT_EQ(surface_info.parent_id, kParentModuleName);
  });

  LaunchParentModule();

  // Have the parent module launch an embedded module
  ParentModuleEmbedsModule();

  // Have the embedded module launch a third module
  EmbeddedModuleLaunchesModule();
}
