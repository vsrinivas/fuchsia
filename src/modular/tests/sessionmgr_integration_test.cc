// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/element/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl_test_base.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/modular/testing/cpp/fake_agent.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>

#include "src/lib/files/glob.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_graphical_presenter.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

constexpr char kTestStoryId[] = "test_story";

using element::annotations::AnnotationEq;
using ::testing::ByRef;
using ::testing::ElementsAre;

class SessionmgrIntegrationTest : public modular_testing::TestHarnessFixture {
 public:
  SessionmgrIntegrationTest()
      : fake_graphical_presenter_(
            modular_testing::FakeGraphicalPresenter::CreateWithDefaultOptions()),
        fake_module_(modular_testing::FakeModule::CreateWithDefaultOptions()) {}

  void LaunchTestHarness() {
    modular_testing::TestHarnessBuilder builder;
    builder.InterceptSessionShell(fake_graphical_presenter_->BuildInterceptOptions());
    builder.InterceptComponent(fake_module_->BuildInterceptOptions());
    builder.UseSessionShellForStoryShellFactory();

    bool graphical_presenter_connected = false;
    fake_graphical_presenter_->set_on_graphical_presenter_connected([&]() {
      graphical_presenter_connected = true;
      fake_graphical_presenter_->set_on_graphical_presenter_error([&](zx_status_t status) {});
    });
    fake_graphical_presenter_->set_on_graphical_presenter_error([&](zx_status_t status) {
      FX_PLOGS(FATAL, status) << "Failed to connect to FakeGraphicalPresenter";
    });

    // Create the test harness and verify the session shell is up
    builder.BuildAndRun(test_harness());

    EXPECT_FALSE(fake_graphical_presenter_->is_running());
    RunLoopUntil([&] { return fake_graphical_presenter_->is_running(); });
    RunLoopUntil([&] { return graphical_presenter_connected; });
  }

  fuchsia::modular::PuppetMasterPtr ConnectToPuppetMaster() {
    fuchsia::modular::PuppetMasterPtr puppet_master;
    fuchsia::modular::testing::ModularService svc;
    svc.set_puppet_master(puppet_master.NewRequest());
    test_harness()->ConnectToModularService(std::move(svc));
    return puppet_master;
  }

  fuchsia::modular::StoryPuppetMasterPtr ControlStory() {
    auto puppet_master = ConnectToPuppetMaster();
    fuchsia::modular::StoryPuppetMasterPtr story_puppet_master;
    puppet_master->ControlStory(kTestStoryId, story_puppet_master.NewRequest());
    return story_puppet_master;
  }

  // Watches for changes to story states on the session shell's StoryProvider and appends new
  // states to |sequence_of_story_states|.
  //
  // Expects that only the story with ID |kTestStoryId| is changed. This story does not have to
  // exist prior to calling WatchStoryStates.
  [[nodiscard]] std::unique_ptr<modular_testing::SimpleStoryProviderWatcher> WatchStoryStates(
      std::vector<fuchsia::modular::StoryState>* sequence_of_story_states) const {
    fuchsia::modular::StoryProvider* story_provider = fake_graphical_presenter_->story_provider();
    EXPECT_TRUE(story_provider != nullptr);

    // Have the StoryProviderWatcher record the sequence of story states it sees.
    auto watcher = std::make_unique<modular_testing::SimpleStoryProviderWatcher>();
    watcher->set_on_change_2([sequence_of_story_states](fuchsia::modular::StoryInfo2 story_info,
                                                        fuchsia::modular::StoryState story_state,
                                                        fuchsia::modular::StoryVisibilityState _) {
      EXPECT_TRUE(story_info.has_id());
      EXPECT_EQ(story_info.id(), kTestStoryId);
      sequence_of_story_states->push_back(story_state);
    });
    watcher->Watch(story_provider, /*on_get_stories=*/nullptr);

    return watcher;
  }

  void LaunchMod(
      fuchsia::modular::StoryPuppetMasterPtr* story_puppet_master,
      fit::function<void(fuchsia::modular::ExecuteResult result)> callback = [](auto result) {
      }) const {
    fuchsia::modular::Intent intent;
    intent.handler = fake_module_->url();
    intent.action = "action";

    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = "modname";
    add_mod.intent = std::move(intent);

    fuchsia::modular::StoryCommand cmd;
    cmd.set_add_mod(std::move(add_mod));

    std::vector<fuchsia::modular::StoryCommand> cmds;
    cmds.push_back(std::move(cmd));

    // Add the module to the story
    (*story_puppet_master)->Enqueue(std::move(cmds));
    (*story_puppet_master)->Execute(std::move(callback));
  }

  void StopStory() {
    fuchsia::modular::StoryControllerPtr story_controller;
    fake_graphical_presenter_->story_provider()->GetController(kTestStoryId,
                                                               story_controller.NewRequest());
    bool stop_called = false;
    story_controller->Stop([&] { stop_called = true; });
    RunLoopUntil([&] { return stop_called; });
  }

  std::unique_ptr<modular_testing::FakeGraphicalPresenter> fake_graphical_presenter_;
  std::unique_ptr<modular_testing::FakeModule> fake_module_;
  std::unique_ptr<modular_testing::FakeAgent> fake_agent_;
};

class SessionmgrIntegrationTestWithoutDefaultHarness : public gtest::TestWithEnvironmentFixture {};

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

// A |FakeComponent| that counts the number of times it has been launched.
class LaunchCountingComponent : public modular_testing::FakeComponent {
 public:
  LaunchCountingComponent()
      : FakeComponent({.url = modular_testing::TestHarnessBuilder::GenerateFakeUrl()}) {}

  int launch_count() const { return launch_count_; }

 protected:
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override { launch_count_++; }

 private:
  int launch_count_ = 0;
};

// Create a service in the test harness that is also not provided by the session environment. Verify
// story mods get the test service from the harness.
TEST_F(SessionmgrIntegrationTest, StoryModsGetServicesFromGlobalEnvironment) {
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
  // It should get the service from the test harness, confirming that the service
  // is accessible.
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
  ASSERT_EQ(fake_intl_property_provider.call_count(), 1);

  // The test_harness version of the service is available also if requested outside of
  // the session scope.
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
  ASSERT_EQ(fake_intl_property_provider.call_count(), 2);
}

TEST_F(SessionmgrIntegrationTest, PresentViewIsCalled) {
  LaunchTestHarness();

  // Add Event Listeners
  bool called_present_view = false;
  fake_graphical_presenter_->set_on_present_view(
      [&](fuchsia::element::ViewSpec view_spec,
          fidl::InterfaceHandle<fuchsia::element::AnnotationController> annotation_controller) {
        called_present_view = true;
      });

  bool called_dismiss = false;
  fake_graphical_presenter_->set_on_dismiss([&] {
    called_dismiss = true;
    fake_graphical_presenter_->CloseAllViewControllers();
  });

  std::vector<fuchsia::modular::StoryState> sequence_of_story_states;
  auto watcher = WatchStoryStates(&sequence_of_story_states);

  auto story_puppet_master = ControlStory();
  LaunchMod(&story_puppet_master);

  // Since this test is using a GraphicalPresenter PresentView should be called.
  RunLoopUntil([&] { return called_present_view; });

  StopStory();

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

TEST_F(SessionmgrIntegrationTest, AnnotationsAreReflectedInAnnotationController) {
  LaunchTestHarness();

  constexpr char kTestAnnotationKey[] = "test_key";
  constexpr char kTestAnnotationValue[] = "test_value";
  constexpr char kTestAnnotationUpdateValue[] = "test_update_value";

  // Add Event Listeners
  bool called_present_view = false;
  fidl::InterfaceHandle<fuchsia::element::AnnotationController> annotation_controller_handle;
  fake_graphical_presenter_->set_on_present_view(
      [&](fuchsia::element::ViewSpec view_spec,
          fidl::InterfaceHandle<fuchsia::element::AnnotationController> annotation_controller) {
        called_present_view = true;
        ASSERT_TRUE(view_spec.has_annotations());

        auto expected_annotation = fuchsia::element::Annotation{
            .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
            .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};
        ASSERT_THAT(view_spec.annotations(), ElementsAre(AnnotationEq(ByRef(expected_annotation))));

        annotation_controller_handle = std::move(annotation_controller);
      });

  // Create the story and add annotations
  std::vector<fuchsia::modular::StoryState> sequence_of_story_states;
  auto watcher = WatchStoryStates(&sequence_of_story_states);

  auto story_puppet_master = ControlStory();

  std::vector<fuchsia::modular::Annotation> annotations;
  annotations.push_back(fuchsia::modular::Annotation{
      .key = kTestAnnotationKey,
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(
          fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue))});
  story_puppet_master->Annotate(std::move(annotations),
                                [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                                  ASSERT_FALSE(result.is_err());
                                });

  LaunchMod(&story_puppet_master);

  // Wait for PresentView to be called
  RunLoopUntil([&] { return called_present_view; });

  // Update Annotations
  std::vector<fuchsia::modular::Annotation> annotation_update;
  annotation_update.push_back(fuchsia::modular::Annotation{
      .key = kTestAnnotationKey,
      .value = std::make_unique<fuchsia::modular::AnnotationValue>(
          fuchsia::modular::AnnotationValue::WithText(kTestAnnotationUpdateValue))});
  bool updated_annotations{false};
  story_puppet_master->Annotate(std::move(annotation_update),
                                [&](fuchsia::modular::StoryPuppetMaster_Annotate_Result result) {
                                  ASSERT_FALSE(result.is_err());
                                  updated_annotations = true;
                                });
  RunLoopUntil([&]() { return updated_annotations; });

  // Get the annotations using the AnnotationController passed to PresentView
  auto annotation_controller = annotation_controller_handle.Bind();
  bool got_annotations{false};
  std::vector<fuchsia::element::Annotation> annotations_to_check;
  annotation_controller->GetAnnotations(
      [&](fuchsia::element::AnnotationController_GetAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        annotations_to_check = std::move(result.response().annotations);
        got_annotations = true;
      });
  RunLoopUntil([&] { return got_annotations; });

  auto expected_annotation = fuchsia::element::Annotation{
      .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationUpdateValue)};
  EXPECT_THAT(annotations_to_check, ElementsAre(AnnotationEq(ByRef(expected_annotation))));
  StopStory();
}

TEST_F(SessionmgrIntegrationTest, DeleteStoryWhenViewControllerIsClosed) {
  static constexpr char kTestStoryId1[] = "test_story_1";
  static constexpr char kTestStoryId2[] = "test_story_2";

  modular_testing::TestHarnessBuilder builder;
  auto fake_graphical_presenter =
      modular_testing::FakeGraphicalPresenter::CreateWithDefaultOptions();

  bool called_present_view = false;
  fake_graphical_presenter->set_on_present_view(
      [&](fuchsia::element::ViewSpec view_spec,
          fidl::InterfaceHandle<fuchsia::element::AnnotationController> annotation_controller) {
        called_present_view = true;
      });

  builder.InterceptSessionShell(fake_graphical_presenter->BuildInterceptOptions());
  builder.UseSessionShellForStoryShellFactory();

  bool graphical_presenter_connected = false;
  fake_graphical_presenter->set_on_graphical_presenter_connected(
      [&]() { graphical_presenter_connected = true; });
  fake_graphical_presenter->set_on_graphical_presenter_error([&](zx_status_t status) {
    FX_PLOGS(FATAL, status) << "Failed to connect to FakeGraphicalPresenter";
  });

  // Register two fake components to be launched as a story mods
  auto fake_module_1 = modular_testing::FakeModule::CreateWithDefaultOptions();
  builder.InterceptComponent(fake_module_1->BuildInterceptOptions());

  auto fake_module_2 = modular_testing::FakeModule::CreateWithDefaultOptions();
  builder.InterceptComponent(fake_module_2->BuildInterceptOptions());

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
  intent.handler = fake_module_1->url();
  intent.action = "action";
  constexpr char kTestModuleName[] = "fake_module";
  modular_testing::AddModToStory(test_harness(), kTestStoryId1, kTestModuleName, std::move(intent));

  ASSERT_FALSE(fake_module_1->is_running());
  RunLoopUntil([&] { return fake_module_1->is_running(); });

  fuchsia::modular::Intent intent_2;
  intent_2.handler = fake_module_2->url();
  intent_2.action = "action";
  constexpr char kTestModuleName2[] = "fake_module_2";
  modular_testing::AddModToStory(test_harness(), kTestStoryId2, kTestModuleName2,
                                 std::move(intent_2));

  ASSERT_FALSE(fake_module_2->is_running());
  RunLoopUntil([&] { return fake_module_2->is_running(); });

  // Since this test is using a GraphicalPresenter PresentView should be called
  RunLoopUntil([&] { return called_present_view; });

  // Close the view controller and wait for the module to stop.
  fake_graphical_presenter->CloseFirstViewController();
  RunLoopUntil([&] { return !fake_module_1->is_running(); });

  // Run the loop until there are the expected number of state changes;
  // having called Stop() is not enough to guarantee seeing all updates.
  RunLoopUntil([&] { return sequence_of_story_states.size() == 6; });

  // Confirm that the story went through the correct sequence of states.
  // Since the test started it, ran it, and stopped it, the sequence is:
  // STOPPED -> RUNNING -> STOPPING -> STOPPED.
  ASSERT_THAT(sequence_of_story_states,
              testing::ElementsAre(
                  fuchsia::modular::StoryState::STOPPED, fuchsia::modular::StoryState::RUNNING,
                  fuchsia::modular::StoryState::STOPPED, fuchsia::modular::StoryState::RUNNING,
                  fuchsia::modular::StoryState::STOPPING, fuchsia::modular::StoryState::STOPPED));

  // Ensure that only the first module was stopped.
  ASSERT_TRUE(fake_module_2->is_running());
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
  FX_LOGS(INFO) << "Waiting for session shell to startup.";
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
    FX_LOGS(INFO) << "Waiting for session shell to shutdown. Iteration: " << i;
    RunLoopUntil([&] { return !session_shell->is_running(); });
    FX_LOGS(INFO) << "Waiting for confirmation from RestartSession().";
    RunLoopUntil([&] { return session_restarted; });
    ASSERT_FALSE(mock_admin.reboot_called()) << "Suspend called on iteration #" << i;
    FX_LOGS(INFO) << "Waiting for session shell to start after restart.";
    RunLoopUntil([&] { return session_shell->is_running(); });
  }
  ASSERT_FALSE(mock_admin.reboot_called());
}

TEST_F(SessionmgrIntegrationTest, RestartSessionAgentOnCrash) {
  LaunchCountingComponent agent;

  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_sessionmgr_config()->set_session_agents({agent.url()});
  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptComponent(agent.BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  // Wait for the agent to start.
  RunLoopUntil([&] { return agent.is_running(); });
  ASSERT_EQ(1, agent.launch_count());

  // Terminate the agent.
  agent.Exit(1, fuchsia::sys::TerminationReason::UNKNOWN);

  // The agent should have restarted at least once.
  RunLoopUntil([&] { return agent.launch_count() >= 2; });
}

TEST_F(SessionmgrIntegrationTest, RestartSessionOnSessionAgentCrash) {
  LaunchCountingComponent session_shell;
  LaunchCountingComponent agent;

  // Configure sessiomgr to restart the session when the agent terminates.
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.mutable_sessionmgr_config()->set_session_agents({agent.url()});
  spec.mutable_sessionmgr_config()->set_restart_session_on_agent_crash({agent.url()});

  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.InterceptSessionShell(session_shell.BuildInterceptOptions());
  builder.InterceptComponent(agent.BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  // Wait for the session to start.
  RunLoopUntil([&] { return session_shell.is_running() && agent.is_running(); });

  // Terminate the agent.
  agent.Exit(1, fuchsia::sys::TerminationReason::UNKNOWN);

  // The session and agent should have restarted at least once.
  RunLoopUntil([&] { return session_shell.launch_count() >= 2 && agent.launch_count() >= 2; });
}

// Tests that agents have access to PuppetMaster during teardown.
// This test creates its own TestHarnessLauncher so it can tear it down before the test ends.
TEST_F(SessionmgrIntegrationTestWithoutDefaultHarness, PuppetMasterInAgentTerminate) {
  static const auto kFakeAgentUrl =
      modular_testing::TestHarnessBuilder::GenerateFakeUrl("test_agent");

  auto fake_agent_ =
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
    builder.InterceptComponent(fake_agent_->BuildInterceptOptions());
    builder.BuildAndRun(test_harness_launcher.test_harness());

    // Wait for the session to start.
    RunLoopUntil([&] { return session_shell->is_running() && fake_agent_->is_running(); });

    puppet_master.set_error_handler([&](zx_status_t /*unused*/) {
      // The agent should have terminated before PuppetMaster is closed.
      EXPECT_TRUE(is_agent_terminate_called);
      is_puppet_master_closed = true;
    });

    // Connect to the PuppetMaster provided to the agent.
    fake_agent_->component_context()->svc()->Connect(puppet_master.NewRequest());

    fake_agent_->set_on_terminate([&]() {
      // PuppetMaster should not have closed before the agent is torn down.
      EXPECT_FALSE(is_puppet_master_closed);
      is_agent_terminate_called = true;
    });

    test_harness_launcher.StopTestHarness();

    // Wait until the agent terminates
    RunLoopUntil([&] { return !fake_agent_->is_running(); });

    RunLoopUntil([&] { return is_agent_terminate_called && is_puppet_master_closed; });

    // The test harness component is torn down once |test_harness_launcher| goes out of scope.
  }
}

// Tests that creating a story before StoryProviderImpl connects to a presentation protocol
// results in the PresentView call being pended and called again once connected.
TEST_F(SessionmgrIntegrationTest, PresentViewBeforePresentationProtocolConnected) {
  modular_testing::TestHarnessBuilder builder;
  auto fake_graphical_presenter =
      modular_testing::FakeGraphicalPresenter::CreateWithDefaultOptions();

  fake_graphical_presenter->set_on_create([&](fit::function<void()> done) {
    // Create the story before the FakeGraphicalPresenter component starts serving its outgoing
    // directory. This ensures that StoryProviderImpl has not yet selected a presentation protocol.
    auto story_puppet_master = ControlStory();
    LaunchMod(&story_puppet_master);
    done();
  });

  bool called_present_view = false;
  fake_graphical_presenter->set_on_present_view(
      [&](fuchsia::element::ViewSpec view_spec,
          fidl::InterfaceHandle<fuchsia::element::AnnotationController> annotation_controller) {
        called_present_view = true;
      });

  bool graphical_presenter_connected = false;
  fake_graphical_presenter->set_on_graphical_presenter_connected(
      [&]() { graphical_presenter_connected = true; });

  fake_graphical_presenter->set_on_graphical_presenter_error([&](zx_status_t status) {
    FX_PLOGS(FATAL, status) << "Failed to connect to FakeGraphicalPresenter";
  });

  builder.InterceptSessionShell(fake_graphical_presenter->BuildInterceptOptions());
  builder.UseSessionShellForStoryShellFactory();

  // Create the test harness and verify the session shell is up
  builder.BuildAndRun(test_harness());

  EXPECT_FALSE(fake_graphical_presenter->is_running());
  RunLoopUntil([&] { return fake_graphical_presenter->is_running(); });

  // StoryProviderImpl should have selected GraphicalPresenter called PresentView.
  RunLoopUntil([&] { return graphical_presenter_connected; });
  RunLoopUntil([&] { return called_present_view; });
}

// Tests that creating and deleting a story before the presentation protocol is chosen as a
// result of the session component exposing its outgoing directory does not cause sessionmgr
// to try to present a pended view for a nonexistent story.
TEST_F(SessionmgrIntegrationTest, PresentViewDeletedStory) {
  modular_testing::TestHarnessBuilder builder;
  fake_graphical_presenter_ = modular_testing::FakeGraphicalPresenter::CreateWithDefaultOptions();

  fit::function<void()> serve_outgoing;
  fake_graphical_presenter_->set_on_create(
      [&](fit::function<void()> done) { serve_outgoing = std::move(done); });

  fake_graphical_presenter_->set_on_present_view(
      [&](fuchsia::element::ViewSpec view_spec,
          fidl::InterfaceHandle<fuchsia::element::AnnotationController> annotation_controller) {
        FX_LOGS(FATAL) << "PresentView should not be called for a view from a deleted story.";
      });

  bool graphical_presenter_connected = false;
  fake_graphical_presenter_->set_on_graphical_presenter_connected(
      [&]() { graphical_presenter_connected = true; });

  builder.InterceptSessionShell(fake_graphical_presenter_->BuildInterceptOptions());
  builder.InterceptComponent(fake_module_->BuildInterceptOptions());
  builder.UseSessionShellForStoryShellFactory();

  // Create the test harness and verify the session shell is up
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return !!serve_outgoing; });

  auto story_puppet_master = ControlStory();

  // Create the story before the FakeGraphicalPresenter component starts serving its outgoing
  // directory. This ensures that StoryProviderImpl has not yet selected a presentation protocol.
  bool created_story{false};
  LaunchMod(&story_puppet_master, [&](const fuchsia::modular::ExecuteResult& result) mutable {
    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
    created_story = true;
  });
  RunLoopUntil([&] { return created_story; });

  bool deleted_story{false};
  auto puppet_master = ConnectToPuppetMaster();
  puppet_master->DeleteStory(kTestStoryId, [&] { deleted_story = true; });
  RunLoopUntil([&] { return deleted_story; });

  serve_outgoing();

  RunLoopUntil([&] { return graphical_presenter_connected; });
}

}  // namespace
