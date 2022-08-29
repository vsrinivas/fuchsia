// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/tests/gfx_integration_tests/pixel_test.h"

#include <zircon/status.h>

namespace integration_tests {

void PixelTest::SetUp() {
  realm_ = std::make_unique<RealmRoot>(SetupRealm());

  scenic_ = realm_->Connect<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([](zx_status_t status) {
    FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
  });

  annotation_registry_ = realm_->Connect<fuchsia::ui::annotation::Registry>();
  annotation_registry_.set_error_handler([](zx_status_t status) {
    FAIL() << "Lost connection to Annotation Registry: " << zx_status_get_string(status);
  });
}

DisplayDimensions PixelTest::GetDisplayDimensions() {
  DisplayDimensions display_dimensions;
  scenic_->GetDisplayInfo([this, &display_dimensions](fuchsia::ui::gfx::DisplayInfo display_info) {
    display_dimensions = {.width = static_cast<float>(display_info.width_in_px),
                          .height = static_cast<float>(display_info.height_in_px)};
    QuitLoop();
  });
  RunLoop();
  return display_dimensions;
}

void PixelTest::Present(scenic::Session* session, zx::time present_time) {
  session->Present(0, [this](auto) { QuitLoop(); });
  ASSERT_FALSE(RunLoopWithTimeout(kPresentTimeout));
}

scenic::Screenshot PixelTest::TakeScreenshot() {
  fuchsia::ui::scenic::ScreenshotData screenshot_out;
  scenic_->TakeScreenshot(
      [this, &screenshot_out](fuchsia::ui::scenic::ScreenshotData screenshot, bool status) {
        EXPECT_TRUE(status) << "Failed to take screenshot";
        screenshot_out = std::move(screenshot);
        QuitLoop();
      });
  EXPECT_FALSE(RunLoopWithTimeout(kScreenshotTimeout)) << "Timed out waiting for screenshot.";
  return scenic::Screenshot(screenshot_out);
}

void PixelTest::RunUntilIndirectPresent(scenic::TestView* view) {
  // Typical sequence of events:
  // 1. We set up a view bound as a |SessionListener|.
  // 2. The view sends its initial |Present| to get itself connected, without
  //    a callback.
  // 3. We call |RunUntilIndirectPresent| which sets a present callback on our
  //    |TestView|.
  // 4. |RunUntilIndirectPresent| runs the message loop, which allows the view to
  //    receive a Scenic event telling us our metrics.
  // 5. In response, the view sets up the scene graph with the test scene.
  // 6. The view calls |Present| with the callback set in |RunUntilIndirectPresent|.
  // 7. The still-running message loop eventually dispatches the present
  //    callback, which quits the loop.

  view->set_present_callback([this](auto) { QuitLoop(); });
  ASSERT_FALSE(RunLoopWithTimeout(kIndirectPresentTimeout));
}

std::unique_ptr<RootSession> PixelTest::SetUpTestSession() {
  auto test_session = std::make_unique<RootSession>(scenic(), GetDisplayDimensions());
  test_session->session.set_error_handler([](auto) { FAIL() << "Session terminated."; });
  return test_session;
}
}  // namespace integration_tests
