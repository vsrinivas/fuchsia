// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_FLUTTER_EMBEDDER_TEST_IP_H_
#define SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_FLUTTER_EMBEDDER_TEST_IP_H_

#include <fuchsia/input/injection/cpp/fidl.h>
#include <fuchsia/ui/lifecycle/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include <vector>

#include "src/lib/ui/base_view/embedded_view_utils.h"
#include "src/ui/input/testing/fake_input_report_device/fake.h"
#include "src/ui/input/testing/fake_input_report_device/reports_reader.h"
#include "src/ui/testing/views/color.h"
#include "src/ui/testing/views/embedder_view.h"

namespace flutter_embedder_test_ip {

/// Defines a list of services that are injected into the test environment. Unlike the
/// injected-services in CMX which are injected per test package, these are injected per test and
/// result in a more hermetic test environment.
const std::vector<std::pair<const char*, const char*>> GetInjectedServices();

// Timeout when waiting on Scenic API calls like |GetDisplayInfo|.
constexpr zx::duration kCallTimeout = zx::sec(5);
// Timeout for Scenic's |TakeScreenshot| FIDL call.
constexpr zx::duration kScreenshotTimeout = zx::sec(10);
// Timeout to fail the test if it goes beyond this duration.
constexpr zx::duration kTestTimeout = zx::min(1);

class FlutterEmbedderTestIp : public gtest::TestWithEnvironmentFixture {
 public:
  // |testing::Test|
  void SetUp() override {
    TestWithEnvironmentFixture::SetUp();

    // Create test-specific launchable services.
    auto services = TestWithEnvironmentFixture::CreateServices();
    for (const auto& service_info : GetInjectedServices()) {
      zx_status_t status =
          services->AddServiceWithLaunchInfo({.url = service_info.second}, service_info.first);
      FX_CHECK(status == ZX_OK) << "Failed to add service " << service_info.first;
    }

    environment_ = CreateNewEnclosingEnvironment("flutter-embedder-ip-tests", std::move(services),
                                                 {.inherit_parent_services = true});
    WaitForEnclosingEnvToStart(environment());

    FX_VLOGS(1) << "Created test environment.";

    // Connects to scenic lifecycle controller in order to shutdown scenic at the end of the test.
    // This ensures the correct ordering of shutdown under CFv1: first scenic, then the fake
    // display controller.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    environment_->ConnectToService<fuchsia::ui::lifecycle::LifecycleController>(
        scenic_lifecycle_controller_.NewRequest());

    environment_->ConnectToService(scenic_.NewRequest());
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });

    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTestTimeout);
  }

  // |testing::Test|
  void TearDown() override {
    // Avoid spurious errors since we are about to kill scenic.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    scenic_.set_error_handler(nullptr);

    zx_status_t terminate_status = scenic_lifecycle_controller_->Terminate();
    FX_CHECK(terminate_status == ZX_OK)
        << "Failed to terminate Scenic with status: " << zx_status_get_string(terminate_status);
  }

  sys::testing::EnclosingEnvironment* environment() { return environment_.get(); }

  fuchsia::ui::views::ViewToken CreatePresentationViewToken() {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

    auto presenter = environment()->ConnectToService<fuchsia::ui::policy::Presenter>();
    presenter.set_error_handler(
        [](zx_status_t status) { FAIL() << "presenter: " << zx_status_get_string(status); });
    presenter->PresentView(std::move(view_holder_token), nullptr);

    return std::move(view_token);
  }

  void RunAppWithArgs(const std::string& component_url,
                      const std::vector<std::string>& component_args = {}) {
    scenic::EmbeddedViewInfo flutter_runner = scenic::LaunchComponentAndCreateView(
        environment()->launcher_ptr(), component_url, component_args);
    flutter_runner.controller.events().OnTerminated = [](auto...) { FAIL(); };

    // Present the view.
    embedder_view_.emplace(scenic::ViewContext{
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get()),
        .view_token = CreatePresentationViewToken(),
    });

    // Embed the view.
    bool is_rendering = false;
    embedder_view_->EmbedView(std::move(flutter_runner),
                              [&is_rendering](fuchsia::ui::gfx::ViewState view_state) {
                                is_rendering = view_state.is_rendering;
                              });
    RunLoopUntil([&is_rendering] { return is_rendering; });
    FX_LOGS(INFO) << "Launched component: " << component_url;
  }

  scenic::Screenshot TakeScreenshot() {
    FX_LOGS(INFO) << "Taking screenshot... ";
    fuchsia::ui::scenic::ScreenshotData screenshot_out;
    scenic_->TakeScreenshot(
        [this, &screenshot_out](fuchsia::ui::scenic::ScreenshotData screenshot, bool status) {
          EXPECT_TRUE(status) << "Failed to take screenshot";
          screenshot_out = std::move(screenshot);
          QuitLoop();
        });
    EXPECT_FALSE(RunLoopWithTimeout(kScreenshotTimeout)) << "Timed out waiting for screenshot.";
    FX_LOGS(INFO) << "Screenshot captured.";

    return scenic::Screenshot(screenshot_out);
  }

  bool TakeScreenshotUntil(scenic::Color color,
                           fit::function<void(std::map<scenic::Color, size_t>)> callback = nullptr,
                           zx::duration timeout = kTestTimeout) {
    return RunLoopWithTimeoutOrUntil(
        [this, &callback, &color] {
          auto screenshot = TakeScreenshot();
          auto histogram = screenshot.Histogram();

          bool color_found = histogram[color] > 0;
          if (color_found && callback != nullptr) {
            callback(std::move(histogram));
          }
          return color_found;
        },
        timeout);
  }

  void RegisterInjectionDevice() {
    registry_ = environment()->ConnectToService<fuchsia::input::injection::InputDeviceRegistry>();

    // Create a FakeInputDevice
    fake_input_device_ = std::make_unique<fake_input_report_device::FakeInputDevice>(
        input_device_ptr_.NewRequest(), dispatcher());

    // Set descriptor
    auto device_descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
    auto touch = device_descriptor->mutable_touch()->mutable_input();
    touch->set_touch_type(fuchsia::input::report::TouchType::TOUCHSCREEN);
    touch->set_max_contacts(10);

    fuchsia::input::report::Axis axis;
    axis.unit.type = fuchsia::input::report::UnitType::NONE;
    axis.unit.exponent = 0;
    axis.range.min = -1000;
    axis.range.max = 1000;

    fuchsia::input::report::ContactInputDescriptor contact;
    contact.set_position_x(axis);
    contact.set_position_y(axis);
    contact.set_pressure(axis);

    touch->mutable_contacts()->push_back(std::move(contact));

    fake_input_device_->SetDescriptor(std::move(device_descriptor));

    // Register the FakeInputDevice
    registry_->Register(std::move(input_device_ptr_));
  }

  // Inject directly into Input Pipeline, using fuchsia.input.injection FIDLs.
  void InjectInput() {
    FX_LOGS(INFO) << "Injecting input... ";

    // Set InputReports to inject. One contact at the center of the display, followed
    // by no contacts.
    fuchsia::input::report::ContactInputReport contact_input_report;
    contact_input_report.set_contact_id(1);
    contact_input_report.set_position_x(0);
    contact_input_report.set_position_y(0);

    fuchsia::input::report::TouchInputReport touch_input_report;
    auto contacts = touch_input_report.mutable_contacts();
    contacts->push_back(std::move(contact_input_report));

    fuchsia::input::report::InputReport input_report;
    input_report.set_touch(std::move(touch_input_report));

    std::vector<fuchsia::input::report::InputReport> input_reports;
    input_reports.push_back(std::move(input_report));

    fuchsia::input::report::TouchInputReport remove_touch_input_report;
    fuchsia::input::report::InputReport remove_input_report;
    remove_input_report.set_touch(std::move(remove_touch_input_report));
    input_reports.push_back(std::move(remove_input_report));
    fake_input_device_->SetReports(std::move(input_reports));

    FX_LOGS(INFO) << "Input dispatched.";
  }

  // Injects an input event, and posts a task to retry after `kTapRetryInterval`.
  //
  // We post the retry task because the first input event we send to Flutter may be lost.
  // The reason the first event may be lost is that there is a race condition as the scene
  // owner starts up.
  //
  // More specifically: in order for our app
  // to receive the injected input, two things must be true before we inject touch input:
  // * The Scenic root view must have been installed, and
  // * The Input Pipeline must have received a viewport to inject touch into.
  //
  // The problem we have is that the `is_rendering` signal that we monitor only guarantees us
  // the view is ready. If the viewport is not ready in Input Pipeline at that time, it will
  // drop the touch event.
  //
  // TODO(fxbug.dev/96986): Improve synchronization and remove retry logic.
  void TryInject() {
    InjectInput();
    async::PostDelayedTask(
        dispatcher(), [this] { TryInject(); }, kTapRetryInterval);
  }

 private:
  const std::unique_ptr<sys::ComponentContext> component_context_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;

  fuchsia::ui::lifecycle::LifecycleControllerSyncPtr scenic_lifecycle_controller_;
  fuchsia::ui::scenic::ScenicPtr scenic_;

  // Wrapped in optional since the view is not created until the middle of SetUp
  std::optional<scenic::EmbedderView> embedder_view_;
  fuchsia::input::injection::InputDeviceRegistryPtr registry_;
  std::unique_ptr<fake_input_report_device::FakeInputDevice> fake_input_device_;
  fuchsia::input::report::InputDevicePtr input_device_ptr_;

  // The typical latency on devices we've tested is ~60 msec. The retry interval is chosen to be
  // a) Long enough that it's unlikely that we send a new tap while a previous tap is still being
  //    processed. That is, it should be far more likely that a new tap is sent because the first
  //    tap was lost, than because the system is just running slowly.
  // b) Short enough that we don't slow down tryjobs.
  //
  // The first property is important to avoid skewing the latency metrics that we collect.
  // For an explanation of why a tap might be lost, see the documentation for TryInject().
  static constexpr auto kTapRetryInterval = zx::sec(1);
};

}  // namespace flutter_embedder_test_ip

#endif  // SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_FLUTTER_EMBEDDER_TEST_IP_H_
