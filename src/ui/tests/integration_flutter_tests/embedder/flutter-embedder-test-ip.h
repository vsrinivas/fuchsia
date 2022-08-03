// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_FLUTTER_EMBEDDER_TEST_IP_H_
#define SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_FLUTTER_EMBEDDER_TEST_IP_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/test/input/cpp/fidl.h>
#include <fuchsia/ui/test/scene/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include <optional>
#include <vector>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/views/color.h"

namespace flutter_embedder_test_ip {

// Timeout for Scenic's |TakeScreenshot| FIDL call.
constexpr zx::duration kScreenshotTimeout = zx::sec(10);
// Timeout to fail the test if it goes beyond this duration.
constexpr zx::duration kTestTimeout = zx::min(1);

class FlutterEmbedderTestIp : public ::loop_fixture::RealLoop,
                              public ::testing::Test,
                              public ::testing::WithParamInterface<std::string> {
 public:
  FlutterEmbedderTestIp() : realm_builder_(component_testing::RealmBuilder::Create()) {
    FX_VLOGS(1) << "Setting up base realm";
    SetUpRealmBase();

    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTestTimeout);
  }

  bool HasViewConnected(
      const fuchsia::ui::observation::geometry::ProviderPtr& geometry_provider,
      std::optional<fuchsia::ui::observation::geometry::ProviderWatchResponse>& watch_response,
      zx_koid_t view_ref_koid);

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

  void BuildRealmAndLaunchApp(const std::string& component_url,
                              const std::vector<std::string>& component_args = {});

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

  // Registers a fake touch screen device with an injection coordinate space
  // spanning [-1000, 1000] on both axes.
  void RegisterTouchScreen();

  // Simulates a tap at location (x, y).
  void InjectTap(int32_t x, int32_t y);

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
  void TryInject(int32_t x, int32_t y);

 private:
  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  void SetUpRealmBase();

  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuchsia::ui::test::input::RegistryPtr input_registry_;
  fuchsia::ui::test::input::TouchScreenPtr fake_touchscreen_;
  fuchsia::ui::test::scene::ProviderPtr scene_provider_;
  fuchsia::ui::observation::geometry::ProviderPtr geometry_provider_;

  // Wrapped in optional since the view is not created until the middle of SetUp
  component_testing::RealmBuilder realm_builder_;
  std::unique_ptr<component_testing::RealmRoot> realm_;

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
