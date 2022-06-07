// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_FLUTTER_EMBEDDER_TEST_H_
#define SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_FLUTTER_EMBEDDER_TEST_H_

#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include <vector>

#include "embedder_view.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
// TODO(fxb/97309): Break color.h dependency.
#include "src/ui/testing/views/color.h"

namespace flutter_embedder_test {

// Timeout for Scenic's |TakeScreenshot| FIDL call.
constexpr zx::duration kScreenshotTimeout = zx::sec(10);
// Timeout to fail the test if it goes beyond this duration.
constexpr zx::duration kTestTimeout = zx::min(1);

class FlutterEmbedderTest : public ::loop_fixture::RealLoop, public ::testing::Test {
 public:
  FlutterEmbedderTest() : realm_builder_(component_testing::RealmBuilder::Create()) {
    FX_VLOGS(1) << "Setting up base realm";
    SetUpRealmBase();

    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTestTimeout);
  }

  embedder_tests::ViewContext CreatePresentationContext() {
    FX_CHECK(scenic()) << "Scenic is not connected.";

    return {
        .session_and_listener_request = scenic::CreateScenicSessionPtrAndListenerRequest(scenic()),
        .view_token = CreatePresentationViewToken(),
    };
  }

  fuchsia::ui::views::ViewToken CreatePresentationViewToken() {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

    auto presenter = realm_->Connect<fuchsia::ui::policy::Presenter>();
    presenter.set_error_handler(
        [](zx_status_t status) { FAIL() << "presenter: " << zx_status_get_string(status); });
    presenter->PresentView(std::move(view_holder_token), nullptr);

    return std::move(view_token);
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

  void BuildRealmAndLaunchApp(const std::string& component_url);

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

  // Inject directly into Root Presenter, using fuchsia.ui.input FIDLs.
  void InjectInput() {
    using fuchsia::ui::input::InputReport;
    // Device parameters
    auto parameters = std::make_unique<fuchsia::ui::input::TouchscreenDescriptor>();
    *parameters = {.x = {.range = {.min = -1000, .max = 1000}},
                   .y = {.range = {.min = -1000, .max = 1000}},
                   .max_finger_id = 10};

    FX_LOGS(INFO) << "Injecting input... ";
    // Register it against Root Presenter.
    fuchsia::ui::input::DeviceDescriptor device{.touchscreen = std::move(parameters)};
    auto registry = realm_->Connect<fuchsia::ui::input::InputDeviceRegistry>();
    fuchsia::ui::input::InputDevicePtr connection;
    registry->RegisterDevice(std::move(device), connection.NewRequest());

    {
      // Inject one input report, then a conclusion (empty) report.
      auto touch = std::make_unique<fuchsia::ui::input::TouchscreenReport>();
      *touch = {.touches = {{.finger_id = 1, .x = 0, .y = 0}}};  // center of display
      InputReport report{.event_time = static_cast<uint64_t>(zx::clock::get_monotonic().get()),
                         .touchscreen = std::move(touch)};
      connection->DispatchReport(std::move(report));
    }

    {
      auto touch = std::make_unique<fuchsia::ui::input::TouchscreenReport>();
      InputReport report{.event_time = static_cast<uint64_t>(zx::clock::get_monotonic().get()),
                         .touchscreen = std::move(touch)};
      connection->DispatchReport(std::move(report));
    }
    FX_LOGS(INFO) << "Input dispatched.";
  }

 private:
  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  void SetUpRealmBase();

  fuchsia::ui::scenic::ScenicPtr scenic_;

  // Wrapped in optional since the view is not created until the middle of SetUp
  std::optional<embedder_tests::EmbedderView> embedder_view_;
  component_testing::RealmBuilder realm_builder_;
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

}  // namespace flutter_embedder_test

#endif  // SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_FLUTTER_EMBEDDER_TEST_H_
