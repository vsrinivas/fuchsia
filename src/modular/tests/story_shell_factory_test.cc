// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/modular/testing/cpp/fake_component.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_story_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"
#include "src/modular/lib/testing/session_shell_impl.h"

namespace {

// An implementation of the fuchsia.modular.StoryShellFactory FIDL service, to
// be used in session shell components in integration tests.
class TestStoryShellFactory : fuchsia::modular::StoryShellFactory {
 public:
  using StoryShellRequest = fidl::InterfaceRequest<fuchsia::modular::StoryShell>;

  TestStoryShellFactory(sys::ComponentContext* const component_context) {
    component_context->outgoing()->AddPublicService(GetHandler());
  }

  virtual ~TestStoryShellFactory() override = default;

  // Produces a handler function that can be used in the outgoing service
  // provider.
  fidl::InterfaceRequestHandler<fuchsia::modular::StoryShellFactory> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // Whenever StoryShellFactory.AttachStory() is called, the supplied callback
  // is invoked with the story ID and StoryShell request.
  void set_on_attach_story(
      fit::function<void(std::string story_id, StoryShellRequest request)> callback) {
    on_attach_story_ = std::move(callback);
  }

  // Whenever StoryShellFactory.DetachStory() is called, the supplied callback
  // is invoked. The return callback of DetachStory() is invoked asynchronously
  // after a delay that can be configured by the client with set_detach_delay().
  void set_on_detach_story(fit::function<void()> callback) {
    on_detach_story_ = std::move(callback);
  }

  // Configures the delay after which the return callback of DetachStory() is
  // invoked. Used to test the timeout behavior of sessionmgr.
  void set_detach_delay(zx::duration detach_delay) { detach_delay_ = detach_delay; }

 private:
  // |StoryShellFactory|
  void AttachStory(std::string story_id, StoryShellRequest request) override {
    on_attach_story_(std::move(story_id), std::move(request));
  }

  // |StoryShellFactory|
  void DetachStory(std::string story_id, fit::function<void()> done) override {
    on_detach_story_();

    // Used to simulate a sluggish shell that hits the timeout.
    async::PostDelayedTask(async_get_default_dispatcher(), std::move(done), detach_delay_);
  }

  fidl::BindingSet<fuchsia::modular::StoryShellFactory> bindings_;
  fit::function<void(std::string story_id, StoryShellRequest request)> on_attach_story_{
      [](std::string, StoryShellRequest) {}};
  fit::function<void()> on_detach_story_{[]() {}};
  zx::duration detach_delay_{};
};

// A basic fake session shell component: gives access to services
// available to session shells in their environment, as well as an
// implementation of fuchsia::modular::SessionShell built for tests.
class TestSessionShell : public modular_testing::FakeComponent {
 public:
  TestSessionShell(modular_testing::FakeComponent::Args args)
      : modular_testing::FakeComponent(std::move(args)) {}
  fuchsia::modular::StoryProvider* story_provider() { return story_provider_.get(); }

  TestStoryShellFactory* story_shell_factory() { return story_shell_factory_.get(); }

 private:
  // |modular_testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    component_context()->svc()->Connect(session_shell_context_.NewRequest());
    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    component_context()->outgoing()->AddPublicService(session_shell_impl_.GetHandler());

    story_shell_factory_ = std::make_unique<TestStoryShellFactory>(component_context());
  }

  modular_testing::SessionShellImpl session_shell_impl_;
  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  std::unique_ptr<TestStoryShellFactory> story_shell_factory_;
};

class StoryShellFactoryTest : public modular_testing::TestHarnessFixture {
 public:
  const std::string story_name = "story1";
  const std::string mod_name = "mod1";

  TestSessionShell* test_session_shell() { return test_session_shell_.get(); }

  // Initializes the session shell, story shell factory, and story shell
  // implementations and starts the modular test harness.
  void InitSession() {
    // The session shell provides the StoryShellFactory protocol.
    fuchsia::modular::testing::TestHarnessSpec spec;
    spec.mutable_basemgr_config()->set_use_session_shell_for_story_shell_factory(true);

    modular_testing::TestHarnessBuilder builder(std::move(spec));
    test_session_shell_ = std::make_unique<TestSessionShell>(modular_testing::FakeComponent::Args{
        .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
        .sandbox_services = {"fuchsia.modular.SessionShellContext",
                             "fuchsia.modular.PuppetMaster"}});
    // Listen for the module that is created in CreateStory().
    test_module_ =
        std::make_unique<modular_testing::FakeComponent>(modular_testing::FakeComponent::Args{
            .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});
    builder.InterceptSessionShell(test_session_shell_->BuildInterceptOptions());
    builder.InterceptComponent(test_module_->BuildInterceptOptions());
    builder.BuildAndRun(test_harness());

    // Wait for our session shell to start.
    RunLoopUntil([this] { return test_session_shell_->is_running(); });

    // Connect to the PuppetMaster service also provided to the session shell.
    fuchsia::modular::testing::ModularService modular_service;
    modular_service.set_puppet_master(puppet_master_.NewRequest());
    test_harness()->ConnectToModularService(std::move(modular_service));
  }

  void CreateStory() {
    // The session shell should be running and connected to PuppetMaster.
    FX_CHECK(test_session_shell_->is_running());
    // The story should not already be created.
    FX_CHECK(!test_module_->is_running());

    // Create a story
    fuchsia::modular::Intent intent;
    intent.handler = test_module_->url();
    intent.action = "action";
    modular_testing::AddModToStory(test_harness(), story_name, mod_name, std::move(intent));

    // Wait for the story to be created.
    RunLoopUntil([this] { return test_module_->is_running(); });
  }

  void DeleteStory() {
    // The session shell should be running and connected to PuppetMaster.
    FX_CHECK(test_session_shell_->is_running());
    // The story should have been previously created through CreateStory.
    FX_CHECK(test_module_->is_running());

    puppet_master_->DeleteStory(story_name, [] {});

    // Wait for the story to be deleted.
    RunLoopUntil([this] { return !test_module_->is_running(); });
  }

  fuchsia::modular::StoryControllerPtr ControlStory() {
    // The story should have been previously created through CreateStory.
    FX_CHECK(test_module_->is_running());

    // Get a story controller.
    fuchsia::modular::StoryControllerPtr story_controller;
    test_session_shell_->story_provider()->GetController(story_name, story_controller.NewRequest());

    return story_controller;
  }

 private:
  // Component URL of the |test_module_| intercepted in InitSession().
  std::string test_module_url_;

  fuchsia::modular::PuppetMasterPtr puppet_master_;
  std::unique_ptr<TestSessionShell> test_session_shell_;
  std::unique_ptr<modular_testing::FakeComponent> test_module_;
};

TEST_F(StoryShellFactoryTest, AttachCalledOnStoryStart) {
  InitSession();

  modular_testing::FakeStoryShell fake_story_shell(modular_testing::FakeComponent::Args{
      .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()});

  // The StoryShellFactory will be asked to attach a StoryShell when the story
  // is started.
  bool is_attached{false};
  test_session_shell()->story_shell_factory()->set_on_attach_story(
      [&](std::string, fidl::InterfaceRequest<fuchsia::modular::StoryShell> request) {
        is_attached = true;
        fake_story_shell.GetHandler()(std::move(request));
      });

  CreateStory();

  // Start and show the story.
  auto story_controller = ControlStory();
  story_controller->RequestStart();

  // Wait for the StoryShellFactory to attach the StoryShell.
  RunLoopUntil([&] { return is_attached; });
};

TEST_F(StoryShellFactoryTest, DetachCalledOnStoryStop) {
  InitSession();

  // The StoryShellFactory will be asked to detach a StoryShell when the story
  // is stopped.
  bool is_detached{false};
  test_session_shell()->story_shell_factory()->set_on_detach_story([&]() { is_detached = true; });

  CreateStory();

  // Start and show the story.
  auto story_controller = ControlStory();
  story_controller->RequestStart();

  // Stop the story.
  story_controller->Stop([]() {});

  // Wait for the StoryShellFactory to detach the StoryShell.
  RunLoopUntil([&] { return is_detached; });
};

TEST_F(StoryShellFactoryTest, DetachCalledOnStoryDelete) {
  InitSession();

  // The StoryShellFactory will be asked to detach a StoryShell when the story
  // is deleted.
  bool is_detached{false};
  test_session_shell()->story_shell_factory()->set_on_detach_story([&]() { is_detached = true; });

  CreateStory();

  // Start and show the story.
  auto story_controller = ControlStory();
  story_controller->RequestStart();

  DeleteStory();

  // Wait for the StoryShellFactory to detach the StoryShell.
  RunLoopUntil([&] { return is_detached; });
};

}  // namespace
