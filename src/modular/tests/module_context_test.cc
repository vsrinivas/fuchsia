// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/modular_test_harness/cpp/fake_module.h>
#include <lib/modular_test_harness/cpp/fake_session_shell.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>
#include <src/lib/fxl/logging.h>

#include "gmock/gmock.h"
#include "peridot/lib/testing/session_shell_impl.h"

using testing::ElementsAre;

namespace {

class ModuleContextTest : public modular::testing::TestHarnessFixture {
 protected:
  void StartSession(modular::testing::TestHarnessBuilder builder) {
    builder.InterceptSessionShell(
        session_shell_.GetOnCreateHandler(),
        {.sandbox_services = {"fuchsia.modular.SessionShellContext"}});

    test_harness().events().OnNewComponent =
        builder.BuildOnNewComponentHandler();
    test_harness()->Run(builder.BuildSpec());

    // Wait for our session shell to start.
    RunLoopUntil([this] { return session_shell_.is_running(); });
  }

  void RestartStory(std::string story_name) {
    fuchsia::modular::StoryControllerPtr story_controller;
    session_shell_.story_provider()->GetController(
        story_name, story_controller.NewRequest());

    bool restarted = false;
    story_controller->Stop([&] {
      story_controller->RequestStart();
      restarted = true;
    });
    RunLoopUntil([&] { return restarted; });
  }

  modular::testing::FakeSessionShell* session_shell() {
    return &session_shell_;
  }

 private:
  modular::testing::FakeSessionShell session_shell_;
};

// A version of FakeModule which captures handled intents in a std::vector<>
// and exposes callbacks triggered on certain lifecycle events.
class FakeModule : public modular::testing::FakeModule {
 public:
  std::vector<fuchsia::modular::Intent> handled_intents;
  fit::function<void()> on_destroy;
  fit::function<void()> on_create;

 private:
  // |modular::testing::FakeModule|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    modular::testing::FakeModule::OnCreate(std::move(startup_info));
    if (on_create)
      on_create();
  }

  // |modular::testing::FakeModule|
  void OnDestroy() override {
    handled_intents.clear();
    if (on_destroy)
      on_destroy();
  }

  // |fuchsia::modular::IntentHandler|
  void HandleIntent(fuchsia::modular::Intent intent) {
    handled_intents.push_back(std::move(intent));
  }
};

// Test that ModuleContext.AddModuleToStory() starts child modules and that
// calling it multiple times for the same child has different behavior if the
// Intent specifies the same handler, versus if it specifies a different
// handler.
TEST_F(ModuleContextTest, AddModuleToStory) {
  modular::testing::TestHarnessBuilder builder;

  struct FakeModuleInfo {
    std::string url;
    FakeModule component;

    fuchsia::modular::ModuleControllerPtr controller;
  };

  FakeModuleInfo parent_module{.url = builder.GenerateFakeUrl("parent_module")};
  FakeModuleInfo child_module1{.url = builder.GenerateFakeUrl("child_module1")};
  FakeModuleInfo child_module2{.url = builder.GenerateFakeUrl("child_module2")};

  builder.InterceptComponent(
      parent_module.component.GetOnCreateHandler(),
      {.url = parent_module.url,
       .sandbox_services = parent_module.component.GetSandboxServices()});
  builder.InterceptComponent(
      child_module1.component.GetOnCreateHandler(),
      {.url = child_module1.url,
       .sandbox_services = child_module1.component.GetSandboxServices()});
  builder.InterceptComponent(
      child_module2.component.GetOnCreateHandler(),
      {.url = child_module2.url,
       .sandbox_services = child_module2.component.GetSandboxServices()});

  StartSession(std::move(builder));
  AddModToStory({.action = "action", .handler = parent_module.url}, "storyname",
                "modname");
  RunLoopUntil([&] { return parent_module.component.is_running(); });

  // Add a single child module.
  fuchsia::modular::ModuleControllerPtr child_module1_controller;
  parent_module.component.module_context()->AddModuleToStory(
      "childmodname", {.action = "action", .handler = child_module1.url},
      child_module1.controller.NewRequest(),
      /*surface_relation=*/nullptr,
      [&](fuchsia::modular::StartModuleStatus status) {
        ASSERT_EQ(status, fuchsia::modular::StartModuleStatus::SUCCESS);
      });
  RunLoopUntil([&] {
    return child_module1.component.is_running() &&
           child_module1.component.handled_intents.size() == 1;
  });
  EXPECT_EQ(child_module1.component.handled_intents.at(0).action, "action");

  // Add the same module again but with a different Intent action.
  bool child_module1_destroyed{false};
  child_module1.component.on_destroy = [&] { child_module1_destroyed = true; };
  parent_module.component.module_context()->AddModuleToStory(
      "childmodname", {.action = "action2", .handler = child_module1.url},
      child_module1.controller.NewRequest(),
      /*surface_relation=*/nullptr,
      [&](fuchsia::modular::StartModuleStatus status) {
        ASSERT_EQ(status, fuchsia::modular::StartModuleStatus::SUCCESS);
      });
  RunLoopUntil(
      [&] { return child_module1.component.handled_intents.size() == 2; });
  EXPECT_EQ(child_module1.component.handled_intents.at(1).action, "action2");
  // At no time should the child module have been destroyed.
  EXPECT_EQ(child_module1_destroyed, false);

  // This time change the handler. Expect the first module to be shut down,
  // and the second to run in its place.
  parent_module.component.module_context()->AddModuleToStory(
      "childmodname", {.action = "action", .handler = child_module2.url},
      child_module2.controller.NewRequest(),
      /*surface_relation=*/nullptr,
      [&](fuchsia::modular::StartModuleStatus status) {
        ASSERT_EQ(status, fuchsia::modular::StartModuleStatus::SUCCESS);
      });
  RunLoopUntil([&] {
    return child_module2.component.is_running() &&
           child_module2.component.handled_intents.size() == 1;
  });
  EXPECT_FALSE(child_module1.component.is_running());
  EXPECT_EQ(child_module2.component.handled_intents.at(0).action, "action");
}

// Test that ModuleContext.RemoveSelfFromStory() has the affect of shutting
// down the module and removing it permanently from the story (if the story is
// restarted, it is not relaunched).
TEST_F(ModuleContextTest, RemoveSelfFromStory) {
  modular::testing::TestHarnessBuilder builder;

  struct FakeModuleInfo {
    std::string url;
    FakeModule component;
  };

  FakeModuleInfo module1{.url = builder.GenerateFakeUrl("module1")};
  FakeModuleInfo module2{.url = builder.GenerateFakeUrl("module2")};

  builder.InterceptComponent(
      module1.component.GetOnCreateHandler(),
      {.url = module1.url,
       .sandbox_services = module1.component.GetSandboxServices()});
  builder.InterceptComponent(
      module2.component.GetOnCreateHandler(),
      {.url = module2.url,
       .sandbox_services = module2.component.GetSandboxServices()});

  StartSession(std::move(builder));
  AddModToStory({.action = "action", .handler = module1.url}, "modname1",
                "storyname");
  AddModToStory({.action = "action", .handler = module2.url}, "modname2",
                "storyname");
  RunLoopUntil([&] {
    return module1.component.is_running() && module2.component.is_running();
  });

  // Instruct module1 to remove itself from the story. Expect to see that
  // module1 is terminated and module2 is not.
  module1.component.module_context()->RemoveSelfFromStory();
  RunLoopUntil([&] { return !module1.component.is_running(); });
  ASSERT_TRUE(module2.component.is_running());

  // Additionally, restarting the story should not result in module1 being
  // restarted whereas it should for module2.
  bool module2_destroyed = false;
  bool module2_restarted = false;
  module2.component.on_destroy = [&] { module2_destroyed = true; };
  module2.component.on_create = [&] { module2_restarted = true; };
  RestartStory("storyname");
  RunLoopUntil([&] { return module2_restarted; });
  EXPECT_FALSE(module1.component.is_running());
  EXPECT_TRUE(module2_destroyed);
}

// Create a story-hosted Entity using ModuleContext, verify that it can be
// updated and that it has a valid Entity reference.
TEST_F(ModuleContextTest, CreateEntity) {
  modular::testing::TestHarnessBuilder builder;

  auto module_url = builder.GenerateFakeUrl("module");
  FakeModule module;
  builder.InterceptComponent(
      module.GetOnCreateHandler(),
      {.url = module_url, .sandbox_services = module.GetSandboxServices()});

  StartSession(std::move(builder));
  AddModToStory({.action = "action", .handler = module_url}, "modname",
                "storyname");
  RunLoopUntil([&] { return module.is_running(); });

  // Create an entity, acquire an Entity handle as well as a reference
  // to it.
  fidl::StringPtr reference;
  fuchsia::modular::EntityPtr entity;
  {
    fuchsia::mem::Buffer buffer;
    ASSERT_TRUE(fsl::VmoFromString("42", &buffer));
    module.module_context()->CreateEntity(
        "entity_type", std::move(buffer), entity.NewRequest(),
        [&](fidl::StringPtr new_reference) {
          ASSERT_FALSE(new_reference.is_null());
          reference = new_reference;
        });
    RunLoopUntil([&] { return !reference.is_null(); });
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
    module.modular_component_context()->GetEntityResolver(
        resolver.NewRequest());
    fuchsia::modular::EntityPtr entity_from_reference;
    resolver->ResolveEntity(reference, entity_from_reference.NewRequest());

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

// A simple story activity watcher implementation.
class TestStoryActivityWatcher : fuchsia::modular::StoryActivityWatcher {
 public:
  using ActivityChangeFn = fit::function<void(
      std::string, std::vector<fuchsia::modular::OngoingActivityType>)>;

  TestStoryActivityWatcher(ActivityChangeFn on_change)
      : on_change_(std::move(on_change)), binding_(this) {}
  ~TestStoryActivityWatcher() override = default;

  void Watch(fuchsia::modular::StoryProvider* const story_provider) {
    story_provider->WatchActivity(binding_.NewBinding());
  }

 private:
  // |fuchsia::modular::StoryActivityWatcher|
  void OnStoryActivityChange(
      std::string story_id,
      std::vector<fuchsia::modular::OngoingActivityType> activities) override {
    on_change_(std::move(story_id), std::move(activities));
  }

  ActivityChangeFn on_change_;
  fidl::Binding<fuchsia::modular::StoryActivityWatcher> binding_;
};

// When a shell registers a watcher for ongoing activities and modules create
// and destroy them, the shell should ge appropriately notified.
TEST_F(ModuleContextTest, OngoingActivity_NotifyOnWatch) {
  modular::testing::TestHarnessBuilder builder;

  struct FakeModuleInfo {
    std::string url;
    FakeModule component;
  };

  FakeModuleInfo module1{.url = builder.GenerateFakeUrl("module1")};
  FakeModuleInfo module2{.url = builder.GenerateFakeUrl("module2")};

  builder.InterceptComponent(
      module1.component.GetOnCreateHandler(),
      {.url = module1.url,
       .sandbox_services = module1.component.GetSandboxServices()});
  builder.InterceptComponent(
      module2.component.GetOnCreateHandler(),
      {.url = module2.url,
       .sandbox_services = module2.component.GetSandboxServices()});

  StartSession(std::move(builder));
  AddModToStory({.action = "action", .handler = module1.url}, "modname1",
                "storyname");
  AddModToStory({.action = "action", .handler = module2.url}, "modname2",
                "storyname");
  RunLoopUntil([&] {
    return module1.component.is_running() && module2.component.is_running();
  });

  std::vector<std::vector<fuchsia::modular::OngoingActivityType>>
      on_change_updates;
  TestStoryActivityWatcher activity_watcher(
      [&](std::string story_id,
          std::vector<fuchsia::modular::OngoingActivityType> activities) {
        ASSERT_EQ(story_id, "storyname");
        on_change_updates.push_back(std::move(activities));
      });

  auto RunLoopUntilActivityUpdate = [&] {
    auto current_size = on_change_updates.size();
    RunLoopUntil([&] { return on_change_updates.size() > current_size; });
  };

  // Watch for activity updates.
  activity_watcher.Watch(session_shell()->story_provider());
  // And expect to see a notification immediately for "storyname".
  RunLoopUntilActivityUpdate();
  EXPECT_THAT(on_change_updates, ElementsAre(ElementsAre()));

  // Now instruct module1 to create an ongoing activity.
  fuchsia::modular::OngoingActivityPtr ongoing_activity1;
  module1.component.module_context()->StartOngoingActivity(
      fuchsia::modular::OngoingActivityType::VIDEO,
      ongoing_activity1.NewRequest());
  RunLoopUntilActivityUpdate();
  EXPECT_THAT(
      on_change_updates,
      ElementsAre(ElementsAre(),
                  ElementsAre(fuchsia::modular::OngoingActivityType::VIDEO)));

  // When module2 creates one also, expect to see both represented.
  fuchsia::modular::OngoingActivityPtr ongoing_activity2;
  module2.component.module_context()->StartOngoingActivity(
      fuchsia::modular::OngoingActivityType::AUDIO,
      ongoing_activity2.NewRequest());
  RunLoopUntilActivityUpdate();
  EXPECT_THAT(
      on_change_updates,
      ElementsAre(ElementsAre(),
                  ElementsAre(fuchsia::modular::OngoingActivityType::VIDEO),
                  ElementsAre(fuchsia::modular::OngoingActivityType::VIDEO,
                              fuchsia::modular::OngoingActivityType::AUDIO)));

  // module1 terminating its activity should result in a new notification.
  ongoing_activity1.Unbind();
  RunLoopUntilActivityUpdate();
  EXPECT_THAT(
      on_change_updates,
      ElementsAre(ElementsAre(),
                  ElementsAre(fuchsia::modular::OngoingActivityType::VIDEO),
                  ElementsAre(fuchsia::modular::OngoingActivityType::VIDEO,
                              fuchsia::modular::OngoingActivityType::AUDIO),
                  ElementsAre(fuchsia::modular::OngoingActivityType::AUDIO)));

  // And lastly terminating module2's activity results in no more activities.
  ongoing_activity2.Unbind();
  RunLoopUntilActivityUpdate();
  EXPECT_THAT(
      on_change_updates,
      ElementsAre(ElementsAre(),
                  ElementsAre(fuchsia::modular::OngoingActivityType::VIDEO),
                  ElementsAre(fuchsia::modular::OngoingActivityType::VIDEO,
                              fuchsia::modular::OngoingActivityType::AUDIO),
                  ElementsAre(fuchsia::modular::OngoingActivityType::AUDIO),
                  ElementsAre()));
}

}  // namespace
