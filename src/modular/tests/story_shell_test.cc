// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/modular_test_harness/cpp/fake_component.h>
#include <lib/modular_test_harness/cpp/fake_story_shell.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <sdk/lib/sys/cpp/component_context.h>
#include <sdk/lib/sys/cpp/service_directory.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>
#include <src/lib/fxl/logging.h>

#include "gmock/gmock.h"
#include "peridot/lib/testing/session_shell_impl.h"

using testing::ElementsAre;

namespace {

// Timeout for all instances of RunLoopWithTimeoutOrUntil()
constexpr auto kTimeout = zx::sec(30);

// A basic fake story shell component: gives access to services
// available to story shells in their environment, as well as an
// implementation of fuchsia::modular::StoryShell built for tests.
class FakeSessionShell : public modular::testing::FakeComponent {
 public:
  ~FakeSessionShell() override = default;

  fuchsia::modular::StoryProvider* story_provider() {
    return story_provider_.get();
  }

 private:
  // |modular::testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    component_context()->svc()->Connect(session_shell_context_.NewRequest());
    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    component_context()->outgoing()->AddPublicService(
        session_shell_impl_.GetHandler());
  }

  modular::testing::SessionShellImpl session_shell_impl_;
  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
};

class StoryShellTest : public modular::testing::TestHarnessFixture {
 protected:
  void StartSession() {
    modular::testing::TestHarnessBuilder builder;

    builder.InterceptSessionShell(
        session_shell_.GetOnCreateHandler(),
        {.sandbox_services = {"fuchsia.modular.SessionShellContext"}});
    builder.InterceptStoryShell(story_shell_.GetOnCreateHandler());

    fake_module_url_ = builder.GenerateFakeUrl("module");
    builder.InterceptComponent(
        [this](fuchsia::sys::StartupInfo startup_info,
               fidl::InterfaceHandle<
                   fuchsia::modular::testing::InterceptedComponent>
                   intercepted_component) {
          intercepted_modules_.push_back(std::make_unique<modular::testing::FakeComponent>());
          intercepted_modules_.back()->GetOnCreateHandler()(
              std::move(startup_info), std::move(intercepted_component));
        },
        {.url = fake_module_url_});

    test_harness().events().OnNewComponent =
        builder.BuildOnNewComponentHandler();
    test_harness()->Run(builder.BuildSpec());

    fuchsia::modular::testing::ModularService request;
    request.set_puppet_master(puppet_master_.NewRequest());
    test_harness()->ConnectToModularService(std::move(request));

    // Wait for our session shell to start.
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [this] { return session_shell_.is_running(); }, kTimeout));
  }

  void AddModToStory(std::string story_name, std::string mod_name,
                     std::string parent_mod_name = "") {
    fuchsia::modular::StoryPuppetMasterPtr story_puppet_master;
    puppet_master_->ControlStory(story_name, story_puppet_master.NewRequest());

    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = mod_name;
    add_mod.intent.handler = fake_module_url_;
    if (!parent_mod_name.empty()) {
      add_mod.surface_parent_mod_name->push_back(parent_mod_name);
    }

    std::vector<fuchsia::modular::StoryCommand> commands(1);
    commands.at(0).set_add_mod(std::move(add_mod));

    story_puppet_master->Enqueue(std::move(commands));
    bool created = false;
    story_puppet_master->Execute(
        [&](fuchsia::modular::ExecuteResult result) { created = true; });

    // Wait for the story to be created.
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return created; }, kTimeout));
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
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return restarted; }, kTimeout));
  }

  fuchsia::modular::PuppetMasterPtr puppet_master_;
  FakeSessionShell session_shell_;
  modular::testing::FakeStoryShell story_shell_;

  // Stories must have modules in them so the stories created above
  // contain fake intercepted modules. This list holds onto them so that
  // they can be successfully launched and don't die immediately.
  std::vector<std::unique_ptr<modular::testing::FakeComponent>> intercepted_modules_;

  std::string fake_module_url_;
};

// Verifies that when the StoryShell writes content to its Link, those data
// are persisted such that when it is restarted it can retrieve the data again.
TEST_F(StoryShellTest, LinkIsPersistent) {
  StartSession();

  AddModToStory("story1", "a_mod");
  // Wait for our story shell to start and initialize.
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return story_shell_.is_initialized(); }, kTimeout));

  // Write link data.
  fuchsia::modular::LinkPtr link;
  story_shell_.story_shell_context()->GetLink(link.NewRequest());
  {
    fuchsia::mem::Buffer buffer;
    ASSERT_TRUE(fsl::VmoFromString("42", &buffer));
    link->Set(/*path=*/nullptr, std::move(buffer));
  }

  // Wait for confirmation that the write was successful.
  bool got_content = false;
  std::string content;
  link->Get(/*path=*/nullptr,
            [&](std::unique_ptr<fuchsia::mem::Buffer> buffer) {
              got_content = true;
              ASSERT_NE(nullptr, buffer.get());
              ASSERT_TRUE(fsl::StringFromVmo(*buffer, &content));
            });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return got_content; }, kTimeout));
  EXPECT_EQ("42", content);

  // Restart the story.
  bool story_shell_destroyed = false;
  story_shell_.set_on_destroy([&] { story_shell_destroyed = true; });
  RestartStory("story1");

  // Wait for it to die and then restart again.
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return story_shell_destroyed; },
                                        kTimeout));
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return story_shell_.is_running(); }, kTimeout));

  // Show that the contents of the Link are still accessible after a restart.
  story_shell_.story_shell_context()->GetLink(link.NewRequest());
  got_content = false;
  content.clear();
  link->Get(/*path=*/nullptr,
            [&](std::unique_ptr<fuchsia::mem::Buffer> buffer) {
              got_content = true;
              ASSERT_NE(nullptr, buffer.get());
              fsl::StringFromVmo(*buffer, &content);
            });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return got_content; }, kTimeout));
  EXPECT_EQ("42", content);
}

TEST_F(StoryShellTest, GetsModuleMetadata) {
  StartSession();

  std::vector<std::string> surface_ids_added;
  story_shell_.set_on_add_surface(
      [&](fuchsia::modular::ViewConnection view_connection,
          fuchsia::modular::SurfaceInfo surface_info) {
        surface_ids_added.push_back(view_connection.surface_id);
      });

  AddModToStory("story1", "mod1");
  AddModToStory("story1", "mod2", {"mod1"} /* surface relation parent */);
  // Wait for the story shell to be notified of the new modules.
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return surface_ids_added.size() == 2; }, kTimeout));
  EXPECT_THAT(surface_ids_added, ElementsAre("mod1", "mod1:mod2"));

  // Stop the story shell and restart it. Expect to see the same mods notified
  // to the story shell in the same order.
  surface_ids_added.clear();
  RestartStory("story1");
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return surface_ids_added.size() == 2; }, kTimeout));
  EXPECT_THAT(surface_ids_added, ElementsAre("mod1", "mod1:mod2"));
}

}  // namespace
