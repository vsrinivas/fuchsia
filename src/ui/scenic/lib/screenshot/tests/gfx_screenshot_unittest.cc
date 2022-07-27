// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screenshot/gfx_screenshot.h"

#include <fuchsia/ui/composition/cpp/fidl.h>

#include <utility>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace screenshot {
namespace test {

class GfxScreenshotTest : public gtest::RealLoopFixture {
 public:
  GfxScreenshotTest() = default;
  void SetUp() override {
    gfx_screenshotter_ = std::make_unique<screenshot::GfxScreenshot>(
        [](fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) {
          fuchsia::ui::scenic::ScreenshotData screenshot_data;
          fuchsia::images::ImageInfo image_info;
          fuchsia::mem::Buffer data_buffer;

          // Create |info|.
          image_info.width = 100u;
          image_info.height = 100u;

          // Create |data|.
          zx::vmo vmo;
          zx_status_t status = zx::vmo::create(4096, 0, &vmo);
          EXPECT_EQ(status, ZX_OK);
          data_buffer.vmo = std::move(vmo);

          screenshot_data.info = image_info;
          screenshot_data.data = std::move(data_buffer);

          callback(std::move(screenshot_data), /*success=*/true);
        },
        [](screenshot::GfxScreenshot* screenshotter) {});
  }

  void TearDown() override {}

  std::unique_ptr<screenshot::GfxScreenshot> gfx_screenshotter_;
};

TEST_F(GfxScreenshotTest, SimpleTest) {
  fuchsia::ui::composition::ScreenshotTakeRequest request;
  request.set_format(fuchsia::ui::composition::ScreenshotFormat::BGRA_RAW);

  fuchsia::ui::composition::ScreenshotTakeResponse take_response;
  bool done = false;

  gfx_screenshotter_->Take(
      std::move(request),
      [&take_response, &done](fuchsia::ui::composition::ScreenshotTakeResponse response) {
        take_response = std::move(response);
        done = true;
      });

  RunLoopUntil([&done] { return done; });

  EXPECT_TRUE(take_response.has_vmo());
  EXPECT_TRUE(take_response.has_size());

  EXPECT_GT(take_response.size().width, 0u);
  EXPECT_GT(take_response.size().height, 0u);
  EXPECT_NE(take_response.vmo(), 0u);
}

}  // namespace test
}  // namespace screenshot
