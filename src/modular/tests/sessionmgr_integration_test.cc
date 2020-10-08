// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl_test_base.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/modular/testing/cpp/fake_agent.h>
#include <lib/modular/testing/cpp/fake_component.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>

#include "src/lib/files/glob.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_graphical_presenter.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {
constexpr char kTestStoryId[] = "test_story";
constexpr char kTestModuleUrl[] = "fuchsia-pkg://example.com/FAKE_MODULE_PKG/fake_module.cmx";

using session::annotations::AnnotationEq;
using ::testing::ByRef;
using ::testing::ElementsAre;

class SessionmgrIntegrationTest : public modular_testing::TestHarnessFixture {
 public:
  std::unique_ptr<modular_testing::FakeGraphicalPresenter> LaunchTestHarness() {
    modular_testing::TestHarnessBuilder builder;
    auto fake_graphical_presenter =
        modular_testing::FakeGraphicalPresenter::CreateWithDefaultOptions();

    builder.InterceptSessionShell(fake_graphical_presenter->BuildInterceptOptions());
    builder.UseSessionShellForStoryShellFactory();

    bool graphical_presenter_connected = false;
    fake_graphical_presenter->set_on_graphical_presenter_connected(
        [&]() { graphical_presenter_connected = true; });
    fake_graphical_presenter->set_on_graphical_presenter_error([&](zx_status_t status) {
      FX_NOTREACHED() << "Failed to connect to FakeGraphicalPresenter";
    });

    // Create the test harness and verify the session shell is up
    builder.BuildAndRun(test_harness());

    EXPECT_FALSE(fake_graphical_presenter->is_running());
    RunLoopUntil([&] { return fake_graphical_presenter->is_running(); });
    RunLoopUntil([&] { return graphical_presenter_connected; });

    return fake_graphical_presenter;
  }

  std::unique_ptr<modular_testing::SimpleStoryProviderWatcher> CreateStory(
      modular_testing::FakeGraphicalPresenter* fake_graphical_presenter,
      fuchsia::modular::StoryPuppetMasterPtr* story_master,
      std::vector<fuchsia::modular::StoryState>* sequence_of_story_states) {
    FX_DCHECK(fake_graphical_presenter != nullptr) << "FakeSessionShell is nullptr";

    // Create a new story using PuppetMaster and start a new story shell.
    // Confirm that PresentView() is called.
    fuchsia::modular::PuppetMasterPtr puppet_master;
    fuchsia::modular::testing::ModularService svc;
    svc.set_puppet_master(puppet_master.NewRequest());
    test_harness()->ConnectToModularService(std::move(svc));

    fuchsia::modular::StoryProvider* story_provider = fake_graphical_presenter->story_provider();
    EXPECT_TRUE(story_provider != nullptr);

    // Have the mock session_shell record the sequence of story states it sees,
    // and confirm that it only sees the correct story id.
    auto watcher = std::make_unique<modular_testing::SimpleStoryProviderWatcher>();
    watcher->set_on_change_2([sequence_of_story_states](fuchsia::modular::StoryInfo2 story_info,
                                                        fuchsia::modular::StoryState story_state,
                                                        fuchsia::modular::StoryVisibilityState _) {
      EXPECT_TRUE(story_info.has_id());
      EXPECT_EQ(story_info.id(), kTestStoryId);
      sequence_of_story_states->push_back(story_state);
    });
    watcher->Watch(story_provider, /*on_get_stories=*/nullptr);
    puppet_master->ControlStory(kTestStoryId, story_master->NewRequest());

    return watcher;
  }

  void LaunchMod(fuchsia::modular::StoryPuppetMaster* story_master) {
    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "mod1";
    add_mod.intent.handler = kTestModuleUrl;

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));

    std::vector<fuchsia::modular::StoryCommand> commands;
    commands.push_back(std::move(command));

    story_master->Enqueue(std::move(commands));
    story_master->Execute([](fuchsia::modular::ExecuteResult result) {});
  }

  void StopStory(std::string story_id,
                 modular_testing::FakeGraphicalPresenter* fake_graphical_presenter) {
    fuchsia::modular::StoryControllerPtr story_controller;
    fake_graphical_presenter->story_provider()->GetController(story_id,
                                                              story_controller.NewRequest());
    bool stop_called = false;
    story_controller->Stop([&] { stop_called = true; });
    RunLoopUntil([&] { return stop_called; });
  }
};

class SessionmgrIntegrationTestWithoutDefaultHarness : public sys::testing::TestWithEnvironment {};

class IntlPropertyProviderImpl : public fuchsia::intl::PropertyProvider {
 public:
  int call_count() { return call_count_; }

 private:
  void GetProfile(fuchsia::intl::PropertyProvider::GetProfileCallback callback) override {
    call_count_++;
    fuchsia::intl::Profile profile;
    callback(std::move(profile));
  }

  int call_count_ = 0;
};

class MockAdmin : public fuchsia::hardware::power::statecontrol::testing::Admin_TestBase {
 public:
  bool reboot_called() { return reboot_called_; }

 private:
  void Reboot(fuchsia::hardware::power::statecontrol::RebootReason reason,
              RebootCallback callback) override {
    ASSERT_FALSE(reboot_called_);
    reboot_called_ = true;
    ASSERT_EQ(fuchsia::hardware::power::statecontrol::RebootReason::SESSION_FAILURE, reason);
    callback(fuchsia::hardware::power::statecontrol::Admin_Reboot_Result::WithResponse(
        fuchsia::hardware::power::statecontrol::Admin_Reboot_Response(ZX_OK)));
  }

  // |TestBase|
  void NotImplemented_(const std::string& name) override {
    FX_NOTIMPLEMENTED() << name << " is not implemented";
  }

  bool reboot_called_ = false;
};

// A |FakeComponent| that invokes a callback when terminating
class FakeComponentWithOnTerminate : public modular_testing::FakeComponent {
 public:
  explicit FakeComponentWithOnTerminate(FakeComponent::Args args)
      : FakeComponent(std::move(args)) {}
  ~FakeComponentWithOnTerminate() override = default;

  void set_on_terminate(fit::function<void()> on_terminate) {
    on_terminate_ = std::move(on_terminate);
  }

 protected:
  // |fuchsia::modular::Lifecycle|
  void Terminate() override {
    on_terminate_();
    modular_testing::FakeComponent::Terminate();
  }

 private:
  fit::function<void()> on_terminate_ = []() {};
};

// Create a service in the test harness that is also provided by the session environment. Verify
// story mods get the session's version of the service, even though the test harness's version of
// the service is still accessible outside of the story/session.
TEST_F(SessionmgrIntegrationTest, StoryModsGetServicesFromSessionEnvironment) {
  modular_testing::TestHarnessBuilder builder;
  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();
  builder.InterceptSessionShell(session_shell->BuildInterceptOptions());

  // Add a fake fuchsia::intl::PropertyProvider to the test harness' environment.
  IntlPropertyProviderImpl fake_intl_property_provider;
  fidl::BindingSet<fuchsia::intl::PropertyProvider> intl_property_provider_bindings;
  builder.AddService(intl_property_provider_bindings.GetHandler(&fake_intl_property_provider));

  // Register a fake component to be launched as a story mod
  auto fake_module_url = modular_testing::TestHarnessBuilder::GenerateFakeUrl("fake_module");
  modular_testing::FakeModule fake_module{
      {.url = fake_module_url, .sandbox_services = {"fuchsia.intl.PropertyProvider"}}};
  builder.InterceptComponent(fake_module.BuildInterceptOptions());

  // Create the test harness and verify the session shell is up
  builder.BuildAndRun(test_harness());
  ASSERT_FALSE(session_shell->is_running());
  RunLoopUntil([&] { return session_shell->is_running(); });

  // Add at least one module to the story. This should launch the fake_module.
  fuchsia::modular::Intent intent;
  intent.handler = fake_module_url;
  intent.action = "action";
  modular_testing::AddModToStory(test_harness(), "fake_story", "fake_modname", std::move(intent));

  ASSERT_FALSE(fake_module.is_running());
  RunLoopUntil([&] { return fake_module.is_running(); });

  // Request a fuchsia::intl::PropertyProvider from the story mod's component_context().
  // It should get the service from the session environment, not the fake
  // version registered in the test_harness, outside the session.
  // fake_intl_property_provider.call_count() should still be zero (0).
  fuchsia::intl::PropertyProviderPtr module_intl_property_provider;
  auto got_module_intl_property_provider =
      fake_module.component_context()->svc()->Connect<fuchsia::intl::PropertyProvider>(
          module_intl_property_provider.NewRequest());
  EXPECT_EQ(got_module_intl_property_provider, ZX_OK);
  bool got_profile_from_module_callback = false;
  zx_status_t get_profile_from_module_status = ZX_OK;
  module_intl_property_provider->GetProfile(
      [&](fuchsia::intl::Profile new_profile) { got_profile_from_module_callback = true; });
  module_intl_property_provider.set_error_handler(
      [&](zx_status_t status) { get_profile_from_module_status = status; });
  RunLoopUntil(
      [&] { return got_profile_from_module_callback || get_profile_from_module_status != ZX_OK; });
  ASSERT_EQ(get_profile_from_module_status, ZX_OK);
  ASSERT_EQ(fake_intl_property_provider.call_count(), 0);

  // And yet, the test_harness version of the service is still available, if requested outside of
  // the session scope. This time fake_intl_property_provider.call_count() should be one (1).
  fuchsia::intl::PropertyProviderPtr intl_property_provider;
  test_harness()->ConnectToEnvironmentService(fuchsia::intl::PropertyProvider::Name_,
                                              intl_property_provider.NewRequest().TakeChannel());

  bool got_profile_callback = false;
  zx_status_t got_profile_error = ZX_OK;
  intl_property_provider.set_error_handler([&](zx_status_t status) { got_profile_error = status; });
  intl_property_provider->GetProfile(
      [&](fuchsia::intl::Profile new_profile) { got_profile_callback = true; });
  RunLoopUntil([&] { return got_profile_callback || got_profile_error != ZX_OK; });
  ASSERT_EQ(got_profile_error, ZX_OK);
  ASSERT_EQ(fake_intl_property_provider.call_count(), 1);
}

TEST_F(SessionmgrIntegrationTest, PresentViewIsCalled) {
  auto fake_graphical_presenter = LaunchTestHarness();

  // Add Event Listeners
  bool called_present_view = false;
  fake_graphical_presenter->set_on_present_view(
      [&](fuchsia::session::ViewSpec view_spec) { called_present_view = true; });

  bool called_dismiss = false;
  fake_graphical_presenter->set_on_dismiss([&] { called_dismiss = true; });

  // Create the story
  fuchsia::modular::StoryPuppetMasterPtr story_master;
  std::vector<fuchsia::modular::StoryState> sequence_of_story_states;
  auto watcher =
      CreateStory(fake_graphical_presenter.get(), &story_master, &sequence_of_story_states);

  LaunchMod(story_master.get());

  // Since this test is using a GraphicalPresenter PresentView should be called.
  RunLoopUntil([&] { return called_present_view; });

  StopStory(kTestStoryId, fake_graphical_presenter.get());

  // Run the loop until there are the expected number of state changes;
  // having called Stop() is not enough to guarantee seeing all updates.
  RunLoopUntil([&] { return sequence_of_story_states.size() == 4; });

  // Confirm that:
  //  a. Dismiss was called.
  //  b. The story went through the correct sequence of states (see StoryState FIDL file for valid
  // state transitions). Since the test started it, ran it, and stopped it, the sequence is:
  // STOPPED -> RUNNING -> STOPPING -> STOPPED.
  ASSERT_TRUE(called_dismiss);
  ASSERT_THAT(sequence_of_story_states,
              testing::ElementsAre(
                  fuchsia::modular::StoryState::STOPPED, fuchsia::modular::StoryState::RUNNING,
                  fuchsia::modular::StoryState::STOPPING, fuchsia::modular::StoryState::STOPPED));
}

TEST_F(SessionmgrIntegrationTest, AnnotationsArePassedToGraphicalPresenter) {
  auto fake_graphical_presenter = LaunchTestHarness();

  constexpr char kTestAnnotationKey[] = "test_key";
  constexpr char kTestAnnotationValue[] = "test_value";
  constexpr char kTestAnnotationUpdateValue[] = "test_update_value";

  // Add Event Listeners
  bool called_present_view = false;
  fake_graphical_presenter->set_on_present_view([&](fuchsia::session::ViewSpec view_spec) {
    called_present_view = true;
    ASSERT_TRUE(view_spec.has_annotations());
    ASSERT_TRUE(view_spec.annotations().has_custom_annotations());

    auto expected_annotation =
        fuchsia::session::Annotation{.key = kTestAnnotationKey,
                                     .value = std::make_unique<fuchsia::session::Value>(
                                         fuchsia::session::Value::WithText(kTestAnnotationValue))};
    ASSERT_THAT(view_spec.annotations().custom_annotations(),
                ElementsAre(AnnotationEq(ByRef(expected_annotation))));
  });

  bool called_on_annotate = false;
  fake_graphical_presenter->set_on_annotate([&](fuchsia::session::Annotations annotations) {
    called_on_annotate = true;
    ASSERT_TRUE(annotations.has_custom_annotations());

    auto expected_annotation = fuchsia::session::Annotation{
        .key = kTestAnnotationKey,
        .value = std::make_unique<fuchsia::session::Value>(
            fuchsia::session::Value::WithText(kTestAnnotationUpdateValue))};
    ASSERT_THAT(annotations.custom_annotations(),
                ElementsAre(AnnotationEq(ByRef(expected_annotation))));
  });

  // Create the story and add annotations
  fuchsia::modular::StoryPuppetMasterPtr story_master;
  std::vector<fuchsia::modular::StoryState> sequence_of_story_states;
  auto watcher =
      CreateStory(fake_graphical_presenter.get(), &story_master, &sequence_of_story_states);

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fuchsia::modular::Annotation{
      .key = kTestAnnotationKey,
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(
          fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue))});
  story_master->Annotate(std::move(annotations),
                         [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                           ASSERT_FALSE(result.is_err());
                         });

  LaunchMod(story_master.get());

  // Wait for PresentView to be called and and verify that on_annotate wasn't called since it
  // should only be called when annotations are updated, not when initally set.
  RunLoopUntil([&] { return called_present_view; });
  ASSERT_FALSE(called_on_annotate);

  // Update Annotations
  std::vector<fuchsia::modular::Annotation> annotation_update;
  annotation_update.push_back(fuchsia::modular::Annotation{
      .key = kTestAnnotationKey,
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(
          fuchsia::modular::AnnotationValue::WithText(kTestAnnotationUpdateValue))});
  story_master->Annotate(std::move(annotation_update),
                         [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                           ASSERT_FALSE(result.is_err());
                         });

  RunLoopUntil([&] { return called_on_annotate; });

  StopStory(kTestStoryId, fake_graphical_presenter.get());
}

TEST_F(SessionmgrIntegrationTest, DeleteStoryWhenViewControllerIsClosed) {
  modular_testing::TestHarnessBuilder builder;
  auto fake_graphical_presenter =
      modular_testing::FakeGraphicalPresenter::CreateWithDefaultOptions();

  // Add Event Listeners
  bool called_present_view = false;
  fake_graphical_presenter->set_on_present_view(
      [&](fuchsia::session::ViewSpec view_spec) { called_present_view = true; });

  bool called_dismiss = false;
  fake_graphical_presenter->set_on_dismiss([&] { called_dismiss = true; });

  // Connect to FakeGraphicalPresenter
  builder.InterceptSessionShell(fake_graphical_presenter->BuildInterceptOptions());
  builder.UseSessionShellForStoryShellFactory();

  bool graphical_presenter_connected = false;
  fake_graphical_presenter->set_on_graphical_presenter_connected(
      [&]() { graphical_presenter_connected = true; });
  fake_graphical_presenter->set_on_graphical_presenter_error([&](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Failed to connect to FakeGraphicalPresenter";
    ASSERT_EQ(status, ZX_OK);
  });

  constexpr char kTestStoryId2[] = "test_story_2";
  constexpr char kTestModuleUrl2[] = "fuchsia-pkg://example.com/FAKE_MODULE_PKG/fake_module_2.cmx";

  // Register two fake components to be launched as a story mods
  modular_testing::FakeModule fake_module_1{{.url = kTestModuleUrl}};
  builder.InterceptComponent(fake_module_1.BuildInterceptOptions());

  modular_testing::FakeModule fake_module_2{{.url = kTestModuleUrl2}};
  builder.InterceptComponent(fake_module_2.BuildInterceptOptions());

  // Create the test harness and verify the session shell is up
  builder.BuildAndRun(test_harness());

  ASSERT_FALSE(fake_graphical_presenter->is_running());
  RunLoopUntil([&] { return fake_graphical_presenter->is_running(); });
  RunLoopUntil([&] { return graphical_presenter_connected; });

  std::vector<fuchsia::modular::StoryState> sequence_of_story_states;
  modular_testing::SimpleStoryProviderWatcher watcher;
  watcher.set_on_change_2([&sequence_of_story_states](fuchsia::modular::StoryInfo2 story_info,
                                                      fuchsia::modular::StoryState story_state,
                                                      fuchsia::modular::StoryVisibilityState _) {
    sequence_of_story_states.push_back(story_state);
  });
  watcher.Watch(fake_graphical_presenter->story_provider(), /*on_get_stories=*/nullptr);

  // Add a modules to two different stories
  fuchsia::modular::Intent intent;
  intent.handler = kTestModuleUrl;
  intent.action = "action";
  constexpr char kTestModuleName[] = "fake_module";
  modular_testing::AddModToStory(test_harness(), kTestStoryId, kTestModuleName, std::move(intent));

  ASSERT_FALSE(fake_module_1.is_running());
  RunLoopUntil([&] { return fake_module_1.is_running(); });

  fuchsia::modular::Intent intent_2;
  intent_2.handler = kTestModuleUrl2;
  intent_2.action = "action";
  constexpr char kTestModuleName2[] = "fake_module_2";
  modular_testing::AddModToStory(test_harness(), kTestStoryId2, kTestModuleName2,
                                 std::move(intent_2));

  ASSERT_FALSE(fake_module_2.is_running());
  RunLoopUntil([&] { return fake_module_2.is_running(); });

  // Since this test is using a GraphicalPresenter PresentView should be called
  RunLoopUntil([&] { return called_present_view; });

  // Close the view controller and wait for the module to stop.
  fake_graphical_presenter->CloseFirstViewController();
  RunLoopUntil([&] { return !fake_module_1.is_running(); });
  
  // Run the loop until there are the expected number of state changes;
  // having called Stop() is not enough to guarantee seeing all updates.
  RunLoopUntil([&] { return sequence_of_story_states.size() == 6; });

  // Confirm that:
  //  a. Dismiss was called.
  //  b. The story went through the correct sequence of states (see StoryState FIDL file for valid
  // state transitions). Since the test started it, ran it, and stopped it, the sequence is:
  // STOPPED -> RUNNING -> STOPPING -> STOPPED.
  ASSERT_THAT(sequence_of_story_states,
              testing::ElementsAre(
                  fuchsia::modular::StoryState::STOPPED, fuchsia::modular::StoryState::RUNNING,
                  fuchsia::modular::StoryState::STOPPED, fuchsia::modular::StoryState::RUNNING,
                  fuchsia::modular::StoryState::STOPPING, fuchsia::modular::StoryState::STOPPED));
  
  // Ensure that only the first module was stopped.
  ASSERT_TRUE(fake_module_2.is_running());
}

// Launch a session shell an ensure that it receives argv configured for it in the Modular Config.
TEST_F(SessionmgrIntegrationTest, SessionShellReceivesComponentArgsFromConfig) {
  const std::string session_shell_url = "fuchsia-pkg://fuchsia.com/fake_shell/#fake_shell.cmx";

  fuchsia::modular::testing::TestHarnessSpec spec;

  fuchsia::modular::session::SessionShellMapEntry entry;
  entry.mutable_config()->mutable_app_config()->set_url(session_shell_url);
  spec.mutable_basemgr_config()->mutable_session_shell_map()->push_back(std::move(entry));
  spec.mutable_basemgr_config()->set_use_session_shell_for_story_shell_factory(true);

  fuchsia::modular::testing::InterceptSpec intercept_spec;
  intercept_spec.set_component_url(session_shell_url);
  spec.mutable_components_to_intercept()->push_back(std::move(intercept_spec));

  fuchsia::modular::session::AppConfig component_arg;
  component_arg.set_url(session_shell_url);
  component_arg.mutable_args()->push_back("foo");
  spec.mutable_sessionmgr_config()->mutable_component_args()->push_back(std::move(component_arg));

  bool session_shell_running = false;
  test_harness().events().OnNewComponent =
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent> component) {
        ASSERT_EQ(startup_info.launch_info.url, session_shell_url);
        ASSERT_TRUE(!!startup_info.launch_info.arguments);
        EXPECT_THAT(startup_info.launch_info.arguments.value(), ::testing::ElementsAre("foo"));
        session_shell_running = true;
      };

  test_harness()->Run(std::move(spec));
  RunLoopUntil([&] { return session_shell_running; });
}

TEST_F(SessionmgrIntegrationTest, RebootCalledIfSessionmgrCrashNumberReachesRetryLimit) {
  MockAdmin mock_admin;
  fidl::BindingSet<fuchsia::hardware::power::statecontrol::Admin> admin_bindings;

  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();
  modular_testing::TestHarnessBuilder builder;
  builder.InterceptSessionShell(session_shell->BuildInterceptOptions());
  builder.AddService(admin_bindings.GetHandler(&mock_admin));
  builder.BuildAndRun(test_harness());

  // kill session_shell
  for (int i = 0; i < 4; i++) {
    RunLoopUntil([&] { return session_shell->is_running(); });
    session_shell->Exit(0);
    RunLoopUntil([&] { return !session_shell->is_running(); });
  }
  // Validate suspend is invoked

  RunLoopUntil([&] { return mock_admin.reboot_called(); });
  EXPECT_TRUE(mock_admin.reboot_called());
}

TEST_F(SessionmgrIntegrationTest, RestartSession) {
  // Setup environment with a suffix to enable globbing for basemgr's debug service
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.set_environment_suffix("test");
  modular_testing::TestHarnessBuilder builder(std::move(spec));

  // Setup a MockAdmin to check if sessionmgr restarts too many times. If the MockAdmin calls
  // suspend, then sessionmgr has reached its retry limit and we've failed to succesfully restart
  // the session.
  MockAdmin mock_admin;
  fidl::BindingSet<fuchsia::hardware::power::statecontrol::Admin> admin_bindings;

  // Use a session shell to determine if a session has been started.
  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();
  builder.InterceptSessionShell(session_shell->BuildInterceptOptions());
  builder.AddService(admin_bindings.GetHandler(&mock_admin));
  builder.BuildAndRun(test_harness());
  RunLoopUntil([&] { return session_shell->is_running(); });

  // Connect to basemgr to call RestartSession
  constexpr char kBasemgrGlobPath[] = "/hub/r/mth_*_test/*/c/basemgr.cmx/*/out/debug/basemgr";
  files::Glob glob(kBasemgrGlobPath);
  ASSERT_EQ(1u, glob.size());
  const std::string path = *glob.begin();
  fuchsia::modular::internal::BasemgrDebugPtr basemgr;
  fdio_service_connect(path.c_str(), basemgr.NewRequest().TakeChannel().release());

  // Restart the session 4 times and show that device suspend is NOT invoked.
  for (int i = 0; i < 4; i++) {
    bool session_restarted = false;
    basemgr->RestartSession([&] { session_restarted = true; });
    RunLoopUntil([&] { return !session_shell->is_running(); });
    RunLoopUntil([&] { return session_restarted; });
    ASSERT_FALSE(mock_admin.reboot_called()) << "Suspend called on iteration #" << i;
    RunLoopUntil([&] { return session_shell->is_running(); });
  }
  ASSERT_FALSE(mock_admin.reboot_called());
}

TEST_F(SessionmgrIntegrationTest, RestartSessionAgentOnCrash) {
  std::string fake_agent_url =
      modular_testing::TestHarnessBuilder::GenerateFakeUrl("test_agent_to_restart");

  int launch_count = 0;

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_sessionmgr_config()->set_session_agents({fake_agent_url});
  modular_testing::TestHarnessBuilder builder(std::move(spec));

  std::unique_ptr<modular_testing::FakeAgent> fake_agent;
  builder.InterceptComponent({
      .url = fake_agent_url,
      .sandbox_services =
          {
              fuchsia::modular::ComponentContext::Name_,
          },
      .launch_handler =
          [&](fuchsia::sys::StartupInfo startup_info,
              fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
                  intercepted_component) mutable {
            launch_count++;
            fake_agent =
                std::make_unique<modular_testing::FakeAgent>(modular_testing::FakeComponent::Args{
                    .url = fake_agent_url,
                });
            fake_agent->BuildInterceptOptions().launch_handler(std::move(startup_info),
                                                               std::move(intercepted_component));
          },
  });
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return !!fake_agent && fake_agent->is_running(); });

  ASSERT_EQ(1, launch_count);

  fake_agent->Exit(1, fuchsia::sys::TerminationReason::UNKNOWN);
  auto old_agent = std::move(fake_agent);
  fake_agent.reset();

  RunLoopUntil([&] { return !!fake_agent && fake_agent->is_running(); });

  ASSERT_EQ(2, launch_count);
}

TEST_F(SessionmgrIntegrationTest, RestartSessionOnSessionAgentCrash) {
  static const auto kFakeAgentUrl =
      modular_testing::TestHarnessBuilder::GenerateFakeUrl("test_agent");

  // Configure sessiomgr to restart the session when the agent terminates.
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_sessionmgr_config()->set_session_agents({kFakeAgentUrl});
  spec.mutable_sessionmgr_config()->set_restart_session_on_agent_crash({kFakeAgentUrl});

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();
  builder.InterceptSessionShell(session_shell->BuildInterceptOptions());
  auto fake_agent =
      std::make_unique<modular_testing::FakeAgent>(modular_testing::FakeComponent::Args{
          .url = kFakeAgentUrl,
          .sandbox_services = modular_testing::FakeAgent::GetDefaultSandboxServices()});
  builder.InterceptComponent(fake_agent->BuildInterceptOptions());

  builder.BuildAndRun(test_harness());

  // Wait for the session to start.
  RunLoopUntil([&] { return session_shell->is_running() && fake_agent->is_running(); });

  // Terminate the agent.
  fake_agent->Exit(1, fuchsia::sys::TerminationReason::UNKNOWN);
  RunLoopUntil([&] { return !fake_agent->is_running(); });

  // The session and agent should have restarted.
  RunLoopUntil([&] { return !session_shell->is_running(); });
  RunLoopUntil([&] { return session_shell->is_running() && fake_agent->is_running(); });
}

// Tests that agents have access to PuppetMaster during teardown.
// This test creates its own TestHarnessLauncher so it can tear it down before the test ends.
TEST_F(SessionmgrIntegrationTestWithoutDefaultHarness, PuppetMasterInAgentTerminate) {
  static const auto kFakeAgentUrl =
      modular_testing::TestHarnessBuilder::GenerateFakeUrl("test_agent");

  auto fake_agent =
      std::make_unique<FakeComponentWithOnTerminate>(modular_testing::FakeComponent::Args{
          .url = kFakeAgentUrl,
          .sandbox_services = {fuchsia::modular::ComponentContext::Name_,
                               fuchsia::modular::PuppetMaster::Name_}});
  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();

  fuchsia::modular::PuppetMasterPtr puppet_master;

  bool is_agent_terminate_called{false};
  bool is_puppet_master_closed{false};

  {
    modular_testing::TestHarnessLauncher test_harness_launcher(
        real_services()->Connect<fuchsia::sys::Launcher>());

    fuchsia::modular::testing::TestHarnessSpec spec;
    spec.mutable_sessionmgr_config()->set_session_agents({kFakeAgentUrl});

    modular_testing::TestHarnessBuilder builder(std::move(spec));
    builder.InterceptSessionShell(session_shell->BuildInterceptOptions());
    builder.InterceptComponent(fake_agent->BuildInterceptOptions());
    builder.BuildAndRun(test_harness_launcher.test_harness());

    // Wait for the session to start.
    RunLoopUntil([&] { return session_shell->is_running() && fake_agent->is_running(); });

    puppet_master.set_error_handler([&](zx_status_t /*unused*/) {
      // The agent should have terminated before PuppetMaster is closed.
      EXPECT_TRUE(is_agent_terminate_called);
      is_puppet_master_closed = true;
    });

    // Connect to the PuppetMaster provided to the agent.
    fake_agent->component_context()->svc()->Connect(puppet_master.NewRequest());

    fake_agent->set_on_terminate([&]() {
      // PuppetMaster should not have closed before the agent is torn down.
      EXPECT_FALSE(is_puppet_master_closed);
      is_agent_terminate_called = true;
    });

    test_harness_launcher.StopTestHarness();

    // Wait until the agent terminates
    RunLoopUntil([&] { return !fake_agent->is_running(); });

    RunLoopUntil([&] { return is_agent_terminate_called && is_puppet_master_closed; });

    // The test harness component is torn down once |test_harness_launcher| goes out of scope.
  }
}

}  // namespace
