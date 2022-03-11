// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/sys/cpp/testing/component_interceptor.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/tools/present_view/testing/fake_integration_test_view.h"
#include "src/ui/tools/present_view/testing/fake_intl_manager.h"
#include "src/ui/tools/present_view/testing/fake_presenter.h"

namespace present_view::test {

constexpr char kEnvironment[] = "present_view_integration_tests";
constexpr char kPresentViewComponentUri[] =
    "fuchsia-pkg://fuchsia.com/present_view_tests#meta/present_view.cmx";
constexpr char kIntlPropertyProviderUri[] =
    "fuchsia-pkg://fuchsia.com/intl_property_manager#meta/intl_property_manager_without_flags.cmx";

// Max timeouts in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kGlobalTimeout = zx::min(10);
constexpr zx::duration kTimeout = zx::min(1);

// This test fixture tests the full present_view component running as a standalone process.
//
// The test fixture provides fake |fuchsia.ui.policy.Presenter| and |fuchsia.ui.app.ViewProvider|
// implementations and services them on its main loop.
//
// Each test creates a hermetic environment and launches a present_view component as a separate
// process inside of it.
class PresentViewIntegrationTest : public gtest::TestWithEnvironmentFixture {
 protected:
  // This type encapsulates the possible "return values" that an executing component can produce.
  // The first type is |std::nullptr_t| to allow indicating a component that is still executing.
  using ComponentReturn =
      std::variant<std::nullptr_t, sys::testing::TerminationResult, zx_status_t>;

  // This type encpsulates a component that is "Running".  This means it is currently running or has
  // stopped at some point in the past.
  struct RunningComponent {
    bool terminated() const { return !std::holds_alternative<std::nullptr_t>(return_val); }

    fuchsia::sys::ComponentControllerPtr controller;
    ComponentReturn return_val;
  };

  PresentViewIntegrationTest()
      : interceptor_(sys::testing::ComponentInterceptor::CreateWithEnvironmentLoader(real_env())),
        fake_presenter_(std::make_unique<testing::FakePresenter>()) {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating. <<\n\n"; },
        kGlobalTimeout);

    // We want to inject our fake components and services into the environment.
    auto services = interceptor_.MakeEnvironmentServices(real_env());
    services->AddService(fake_presenter_->GetHandler());

    // Hook various types of component launches in order to deliver pre-programmed behaviors.
    EXPECT_TRUE(interceptor_.InterceptURL(
        testing::kNonexistentViewUri, "",
        [](fuchsia::sys::StartupInfo /*startup_info*/,
           std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component) {
          // Simulate a failure to find the package.
          intercepted_component->Exit(-1, fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND);
        }));
    EXPECT_TRUE(interceptor_.InterceptURL(
        testing::kFakeViewUri, "",
        [this](fuchsia::sys::StartupInfo startup_info,
               std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component) {
          fake_view_ = std::make_unique<testing::FakeIntegrationTestView>(
              std::move(startup_info), std::move(intercepted_component));
        }));
    EXPECT_TRUE(interceptor_.InterceptURL(
        kIntlPropertyProviderUri, "",
        [this](fuchsia::sys::StartupInfo startup_info,
               std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component) {
          fake_intl_manager_ = std::make_unique<testing::FakeIntlManager>(
              std::move(startup_info), std::move(intercepted_component));
        }));

    // Create the environment used in the test.
    environment_ = CreateNewEnclosingEnvironment(kEnvironment, std::move(services));
    WaitForEnclosingEnvToStart(environment_.get());
  }

  bool RunLoopWithTestTimeoutOrUntil(fit::function<bool()> condition) {
    return RunLoopWithTimeoutOrUntil(std::move(condition), kTimeout, zx::duration::infinite());
  }

  std::unique_ptr<RunningComponent> LaunchComponent(std::string url,
                                                    std::vector<std::string> args) {
    fuchsia::sys::LaunchInfo launch_info{
        .url = std::move(url),
        .arguments = fidl::VectorPtr{std::move(args)},
    };

    auto component = std::make_unique<RunningComponent>();
    auto& component_controller = component->controller;
    component_controller.events().OnTerminated =
        [c = component.get()](int64_t return_code, fuchsia::sys::TerminationReason reason) {
          // Don't listen for the PEER_DISCONNECTED error at the end when the channel closes.
          //
          // PEER_DISCONNECTED always occurs at the end of a channel session, even when it closes
          // normally.  Listening for it in this case would stomp the |TerminationResult| with a
          // ZX_OK.
          c->controller.set_error_handler(nullptr);
          c->return_val.emplace<sys::testing::TerminationResult>(sys::testing::TerminationResult{
              .return_code = return_code,
              .reason = reason,
          });
        };
    component_controller.set_error_handler(
        [c = component.get()](zx_status_t status) { c->return_val.emplace<zx_status_t>(status); });

    // Now that event handlers are established, launch the component in the
    // hermetic environment.
    environment_->CreateComponent(std::move(launch_info), component_controller.NewRequest());

    return component;
  }

  void TerminateComponent(RunningComponent* component) {
    component->controller->Kill();
    RunLoopWithTestTimeoutOrUntil([&component] { return component->terminated(); });
  }

  std::unique_ptr<RunningComponent> LaunchPresentView(std::vector<std::string> args) {
    return LaunchComponent(kPresentViewComponentUri, std::move(args));
  }

  std::unique_ptr<RunningComponent> RunPresentViewUntilTerminated(std::vector<std::string> args) {
    std::unique_ptr<RunningComponent> present_view = LaunchPresentView(std::move(args));
    RunLoopWithTestTimeoutOrUntil([&present_view] { return present_view->terminated(); });

    return present_view;
  }

  sys::testing::ComponentInterceptor interceptor_;

  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;

  std::unique_ptr<testing::FakeIntlManager> fake_intl_manager_;
  std::unique_ptr<testing::FakePresenter> fake_presenter_;
  std::unique_ptr<testing::FakeIntegrationTestView> fake_view_;
};

TEST_F(PresentViewIntegrationTest, NoParams) {
  // Passing no parameters does nothing (but prints a warning).
  //
  // present_view should exit, and neither create a token pair nor connect to
  // either of the FIDL interfaces.
  std::unique_ptr<RunningComponent> present_view = RunPresentViewUntilTerminated({});
  auto* result = std::get_if<sys::testing::TerminationResult>(&present_view->return_val);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(fuchsia::sys::TerminationReason::EXITED, result->reason);
  EXPECT_EQ(1, result->return_code);
  ASSERT_NE(fake_presenter_, nullptr);
  EXPECT_FALSE(fake_presenter_->bound());
  EXPECT_FALSE(fake_presenter_->presentation());
  ASSERT_EQ(fake_view_, nullptr);
}

TEST_F(PresentViewIntegrationTest, NoPositionalParams) {
  // Passing no *positional* parameters does nothing, even with otherwise valid
  // parameters specified.
  //
  // present_view should exit, and neither create a token pair nor connect to
  // either of the FIDL interfaces.
  std::unique_ptr<RunningComponent> present_view =
      RunPresentViewUntilTerminated({{"--locale=en-US"}});
  auto* result = std::get_if<sys::testing::TerminationResult>(&present_view->return_val);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(fuchsia::sys::TerminationReason::EXITED, result->reason);
  EXPECT_EQ(1, result->return_code);
  ASSERT_NE(fake_presenter_, nullptr);
  EXPECT_FALSE(fake_presenter_->bound());
  EXPECT_FALSE(fake_presenter_->presentation());
  ASSERT_EQ(fake_view_, nullptr);
}

TEST_F(PresentViewIntegrationTest, NonexistentComponentURI) {
  // Non-existing component URIs are invalid and cause present_view to fail.
  //
  // present_view should create a token pair and pass one end to |Presenter|,
  // but terminate itself once the specified component fails to launch.
  std::unique_ptr<RunningComponent> present_view =
      RunPresentViewUntilTerminated({{testing::kNonexistentViewUri}});
  auto* result = std::get_if<sys::testing::TerminationResult>(&present_view->return_val);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(fuchsia::sys::TerminationReason::EXITED, result->reason);
  EXPECT_EQ(1, result->return_code);

  // bound signals come asynchronously; wait for them to settle before verifying.
  EXPECT_TRUE(RunLoopWithTestTimeoutOrUntil([this] {
    const bool objects_valid = fake_presenter_ && fake_presenter_->presentation().has_value();
    return objects_valid && !fake_presenter_->bound() && !fake_presenter_->presentation()->bound();
  }));
  // peer_disconnected signals come asynchronously; wait for them to settle before verifying.
  EXPECT_TRUE(RunLoopWithTestTimeoutOrUntil([this] {
    const bool objects_valid = fake_presenter_ && fake_presenter_->presentation().has_value();
    return objects_valid && fake_presenter_->presentation()->peer_disconnected();
  }));

  // All state is settled.  Verify final state.
  ASSERT_EQ(fake_view_, nullptr);  // This test case has no view.
  EXPECT_TRUE(fake_presenter_->presentation()->token().value.is_valid());
}

class PresentViewLaunchTest : public PresentViewIntegrationTest,
                              public ::testing::WithParamInterface<std::vector<std::string>> {};

TEST_P(PresentViewLaunchTest, Launch) {
  std::vector<std::string> present_view_args = GetParam();
  // present_view should create a token pair and launch the specified component,
  // passing one end to |Presenter| and the other end to a |ViewProvider| from
  // the component.
  std::unique_ptr<RunningComponent> present_view = LaunchPresentView(std::move(present_view_args));

  // Run the loop until both tokens have been created.
  // Creating tokens implies binding interfaces, so don't explicitly wait on bound signals.
  EXPECT_TRUE(RunLoopWithTestTimeoutOrUntil([this] {
    const bool objects_valid = fake_presenter_ && fake_presenter_->presentation().has_value();
    return objects_valid && fake_presenter_->presentation()->token().value.is_valid();
  }));
  EXPECT_TRUE(RunLoopWithTestTimeoutOrUntil(
      [this] { return fake_view_ && fake_view_->token().value.is_valid(); }));

  // Check steady state -- that interfaces are bound correctly.
  auto* running_result = std::get_if<std::nullptr_t>(&present_view->return_val);
  ASSERT_NE(running_result, nullptr);
  ASSERT_NE(fake_presenter_, nullptr);
  ASSERT_TRUE(fake_presenter_->presentation());
  EXPECT_TRUE(fake_presenter_->bound());
  EXPECT_FALSE(fake_presenter_->presentation()->bound());
  EXPECT_FALSE(fake_presenter_->presentation()->peer_disconnected());
  ASSERT_NE(fake_view_, nullptr);
  EXPECT_FALSE(fake_view_->killed());
  EXPECT_TRUE(fake_view_->bound());
  EXPECT_FALSE(fake_view_->peer_disconnected());

  // Check steady state -- that correlated tokens exist.
  auto& view_holder_token = fake_presenter_->presentation()->token();
  auto& view_token = fake_view_->token();
  EXPECT_TRUE(view_holder_token.value);
  EXPECT_TRUE(view_token.value);
  EXPECT_EQ(fsl::GetKoid(view_token.value.get()),
            fsl::GetRelatedKoid(view_holder_token.value.get()));
  EXPECT_EQ(fsl::GetKoid(view_holder_token.value.get()),
            fsl::GetRelatedKoid(view_token.value.get()));

  // Terminate `present_view` which should also terminate the view-providing component.
  TerminateComponent(present_view.get());
  auto* killed_result = std::get_if<sys::testing::TerminationResult>(&present_view->return_val);
  ASSERT_NE(killed_result, nullptr);
  EXPECT_EQ(fuchsia::sys::TerminationReason::EXITED, killed_result->reason);
  EXPECT_EQ(ZX_TASK_RETCODE_SYSCALL_KILL, killed_result->return_code);

  // killed signals come asynchronously; wait for them to settle before verifying.
  EXPECT_TRUE(RunLoopWithTestTimeoutOrUntil([this] { return fake_view_ && fake_view_->killed(); }));
  // bound signals come asynchronously; wait for them to settle before verifying.
  EXPECT_TRUE(RunLoopWithTestTimeoutOrUntil([this] {
    const bool objects_valid =
        fake_view_ && fake_presenter_ && fake_presenter_->presentation().has_value();
    return objects_valid && !fake_view_->bound() && !fake_presenter_->bound() &&
           !fake_presenter_->presentation()->bound();
  }));
  // peer_disconnected signals come asynchronously; wait for them to settle before verifying.
  EXPECT_TRUE(RunLoopWithTestTimeoutOrUntil([this] {
    const bool objects_valid =
        fake_view_ && fake_presenter_ && fake_presenter_->presentation().has_value();
    return objects_valid && !fake_view_->peer_disconnected() /* Wait was cancelled by Kill() */ &&
           fake_presenter_->presentation()->peer_disconnected();
  }));

  // All state is settled.  Verify final state.
  EXPECT_TRUE(!fake_view_->token().value.is_valid()); /* Token was destroyed by Kill() */
  EXPECT_TRUE(fake_presenter_->presentation()->token().value.is_valid());
}

INSTANTIATE_TEST_SUITE_P(DifferentArgs, PresentViewLaunchTest,
                         ::testing::Values(std::vector<std::string>{{testing::kFakeViewUri}},
                                           std::vector<std::string>{{"--locale=en-US"},
                                                                    {testing::kFakeViewUri}}));

}  // namespace present_view::test
