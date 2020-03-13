// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>

#include "gmock/gmock.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

using testing::ElementsAre;

namespace {

class ModuleContextTest : public modular_testing::TestHarnessFixture {
 protected:
  ModuleContextTest()
      : session_shell_(modular_testing::FakeSessionShell::CreateWithDefaultOptions()) {}

  void StartSession(modular_testing::TestHarnessBuilder builder) {
    builder.InterceptSessionShell(session_shell_->BuildInterceptOptions());
    builder.BuildAndRun(test_harness());

    // Wait for our session shell to start.
    RunLoopUntil([this] { return session_shell_->is_running(); });
  }

  void RestartStory(std::string story_name) {
    fuchsia::modular::StoryControllerPtr story_controller;
    session_shell_->story_provider()->GetController(story_name, story_controller.NewRequest());

    bool restarted = false;
    story_controller->Stop([&] {
      story_controller->RequestStart();
      restarted = true;
    });
    RunLoopUntil([&] { return restarted; });
  }

 private:
  std::unique_ptr<modular_testing::FakeSessionShell> session_shell_;
};

// A version of FakeModule which captures handled intents in a std::vector<>
// and exposes callbacks triggered on certain lifecycle events.
class TestModule : public modular_testing::FakeModule {
 public:
  explicit TestModule(std::string module_name = "")
      : modular_testing::FakeModule(
            {.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(module_name),
             .sandbox_services = modular_testing::FakeModule::GetDefaultSandboxServices()},
            [this](fuchsia::modular::Intent intent) {
              handled_intents.push_back(std::move(intent));
            }) {}
  std::vector<fuchsia::modular::Intent> handled_intents;
  fit::function<void()> on_destroy;
  fit::function<void()> on_create;
  fuchsia::modular::ModuleControllerPtr controller;

 private:
  // |modular_testing::FakeModule|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    modular_testing::FakeModule::OnCreate(std::move(startup_info));
    if (on_create)
      on_create();
  }

  // |modular_testing::FakeModule|
  void OnDestroy() override {
    handled_intents.clear();
    if (on_destroy)
      on_destroy();
  }
};

// Test that ModuleContext.AddModuleToStory() starts child modules and that
// calling it multiple times for the same child has different behavior if the
// Intent specifies the same handler, versus if it specifies a different
// handler.
TEST_F(ModuleContextTest, AddModuleToStory) {
  modular_testing::TestHarnessBuilder builder;

  TestModule parent_module("parent_module");
  TestModule child_module1("child_module1");
  TestModule child_module2("child_module2");
  builder.InterceptComponent(parent_module.BuildInterceptOptions());
  builder.InterceptComponent(child_module1.BuildInterceptOptions());
  builder.InterceptComponent(child_module2.BuildInterceptOptions());

  StartSession(std::move(builder));
  modular_testing::AddModToStory(test_harness(), "storyname", "modname",
                                 {.action = "action", .handler = parent_module.url()});
  RunLoopUntil([&] { return parent_module.is_running(); });

  // Add a single child module.
  parent_module.module_context()->AddModuleToStory(
      "childmodname", {.action = "action", .handler = child_module1.url()},
      child_module1.controller.NewRequest(),
      /*surface_relation=*/nullptr, [&](fuchsia::modular::StartModuleStatus status) {
        ASSERT_EQ(status, fuchsia::modular::StartModuleStatus::SUCCESS);
      });
  RunLoopUntil(
      [&] { return child_module1.is_running() && child_module1.handled_intents.size() == 1; });
  EXPECT_EQ(child_module1.handled_intents.at(0).action, "action");

  // Add the same module again but with a different Intent action.
  bool child_module1_destroyed{false};
  child_module1.on_destroy = [&] { child_module1_destroyed = true; };
  parent_module.module_context()->AddModuleToStory(
      "childmodname", {.action = "action2", .handler = child_module1.url()},
      child_module1.controller.NewRequest(),
      /*surface_relation=*/nullptr, [&](fuchsia::modular::StartModuleStatus status) {
        ASSERT_EQ(status, fuchsia::modular::StartModuleStatus::SUCCESS);
      });
  RunLoopUntil([&] { return child_module1.handled_intents.size() == 2; });
  EXPECT_EQ(child_module1.handled_intents.at(1).action, "action2");
  // At no time should the child module have been destroyed.
  EXPECT_EQ(child_module1_destroyed, false);

  // This time change the handler. Expect the first module to be shut down,
  // and the second to run in its place.
  parent_module.module_context()->AddModuleToStory(
      "childmodname", {.action = "action", .handler = child_module2.url()},
      child_module2.controller.NewRequest(),
      /*surface_relation=*/nullptr, [&](fuchsia::modular::StartModuleStatus status) {
        ASSERT_EQ(status, fuchsia::modular::StartModuleStatus::SUCCESS);
      });
  RunLoopUntil(
      [&] { return child_module2.is_running() && child_module2.handled_intents.size() == 1; });
  EXPECT_FALSE(child_module1.is_running());
  EXPECT_EQ(child_module2.handled_intents.at(0).action, "action");
}

// Test that ModuleContext.RemoveSelfFromStory() has the affect of shutting
// down the module and removing it permanently from the story (if the story is
// restarted, it is not relaunched).
TEST_F(ModuleContextTest, RemoveSelfFromStory) {
  modular_testing::TestHarnessBuilder builder;

  TestModule module1("module1");
  TestModule module2("module2");
  builder.InterceptComponent(module1.BuildInterceptOptions());
  builder.InterceptComponent(module2.BuildInterceptOptions());

  StartSession(std::move(builder));
  modular_testing::AddModToStory(test_harness(), "storyname", "modname1",
                                 {.action = "action", .handler = module1.url()});
  modular_testing::AddModToStory(test_harness(), "storyname", "modname2",
                                 {.action = "action", .handler = module2.url()});
  RunLoopUntil([&] { return module1.is_running() && module2.is_running(); });

  // Instruct module1 to remove itself from the story. Expect to see that
  // module1 is terminated and module2 is not.
  module1.module_context()->RemoveSelfFromStory();
  RunLoopUntil([&] { return !module1.is_running(); });
  ASSERT_TRUE(module2.is_running());

  // Additionally, restarting the story should not result in module1 being
  // restarted whereas it should for module2.
  bool module2_destroyed = false;
  bool module2_restarted = false;
  module2.on_destroy = [&] { module2_destroyed = true; };
  module2.on_create = [&] { module2_restarted = true; };
  RestartStory("storyname");
  RunLoopUntil([&] { return module2_restarted; });
  EXPECT_FALSE(module1.is_running());
  EXPECT_TRUE(module2_destroyed);
}

// Create a story-hosted Entity using ModuleContext, verify that it can be
// updated and that it has a valid Entity reference.
TEST_F(ModuleContextTest, CreateEntity) {
  modular_testing::TestHarnessBuilder builder;

  TestModule module;
  builder.InterceptComponent(module.BuildInterceptOptions());

  StartSession(std::move(builder));
  modular_testing::AddModToStory(test_harness(), "storyname", "modname",
                                 {.action = "action", .handler = module.url()});
  RunLoopUntil([&] { return module.is_running(); });

  // Create an entity, acquire an Entity handle as well as a reference
  // to it.
  fidl::StringPtr reference;
  fuchsia::modular::EntityPtr entity;
  {
    fuchsia::mem::Buffer buffer;
    ASSERT_TRUE(fsl::VmoFromString("42", &buffer));
    module.module_context()->CreateEntity("entity_type", std::move(buffer), entity.NewRequest(),
                                          [&](fidl::StringPtr new_reference) {
                                            ASSERT_TRUE(new_reference.has_value());
                                            reference = new_reference;
                                          });
    RunLoopUntil([&] { return reference.has_value(); });
  }

  // Get the types and value from the handle returned by CreateEntity() and
  // observe they are accurate.
  {
    std::vector<std::string> types;
    bool gettypes_done = false;
    entity->GetTypes([&](auto entity_types) {
      types = entity_types;
      gettypes_done = true;
    });
    fuchsia::mem::Buffer buffer;
    bool getdata_done = false;
    entity->GetData("entity_type", [&](auto data) {
      ASSERT_TRUE(data);
      buffer = std::move(*data);
      getdata_done = true;
    });
    RunLoopUntil([&] { return gettypes_done && getdata_done; });
    EXPECT_THAT(types, ElementsAre("entity_type"));

    std::string value;
    ASSERT_TRUE(fsl::StringFromVmo(buffer, &value));
    EXPECT_EQ(value, "42");
  }

  // Get an Entity handle using the reference returned by CreateEntity().
  {
    fuchsia::modular::EntityResolverPtr resolver;
    module.modular_component_context()->GetEntityResolver(resolver.NewRequest());
    fuchsia::modular::EntityPtr entity_from_reference;
    ASSERT_TRUE(reference.has_value());
    resolver->ResolveEntity(reference.value(), entity_from_reference.NewRequest());

    std::vector<std::string> types;
    bool gettypes_done = false;
    entity_from_reference->GetTypes([&](auto entity_types) {
      types = entity_types;
      gettypes_done = true;
    });
    fuchsia::mem::Buffer buffer;
    bool getdata_done = false;
    entity_from_reference->GetData("entity_type", [&](auto data) {
      ASSERT_TRUE(data);
      buffer = std::move(*data);
      getdata_done = true;
    });
    RunLoopUntil([&] { return gettypes_done && getdata_done; });
    EXPECT_THAT(types, ElementsAre("entity_type"));

    std::string value;
    ASSERT_TRUE(fsl::StringFromVmo(buffer, &value));
    EXPECT_EQ(value, "42");
  }

  // Update the entity and observe its value changed.
  {
    fuchsia::mem::Buffer new_value;
    ASSERT_TRUE(fsl::VmoFromString("43", &new_value));
    bool writedata_done = false;
    entity->WriteData("entity_type", std::move(new_value), [&](auto status) {
      ASSERT_EQ(status, fuchsia::modular::EntityWriteStatus::OK);
      writedata_done = true;
    });
    bool getdata_done = false;
    fuchsia::mem::Buffer current_value;
    entity->GetData("entity_type", [&](auto data) {
      ASSERT_TRUE(data);
      current_value = std::move(*data);
      getdata_done = true;
    });
    RunLoopUntil([&] { return getdata_done; });
    EXPECT_TRUE(writedata_done);
    std::string current_value_str;
    ASSERT_TRUE(fsl::StringFromVmo(current_value, &current_value_str));
    EXPECT_EQ(current_value_str, "43");
  }
}

}  // namespace
