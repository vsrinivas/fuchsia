// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/singleton_display_service.h"

#include <zircon/pixelformat.h>

#include <gtest/gtest.h>

namespace scenic_impl {
namespace display {
namespace test {

TEST(SingletonDisplayService, Request) {
  auto display = std::make_shared<Display>(
      0, 777, 555, 77, 55, std::vector<zx_pixel_format_t>{ZX_PIXEL_FORMAT_ARGB_8888});
  auto singleton = std::make_unique<SingletonDisplayService>(display);

  uint32_t width_in_px = 0;
  uint32_t height_in_px = 0;
  uint32_t width_in_mm = 0;
  uint32_t height_in_mm = 0;
  float dpr_x = 0.f;
  float dpr_y = 0.f;

  singleton->GetMetrics([&](::fuchsia::ui::display::singleton::Metrics info) {
    ASSERT_TRUE(info.has_extent_in_px());
    width_in_px = info.extent_in_px().width;
    height_in_px = info.extent_in_px().height;
    ASSERT_TRUE(info.has_extent_in_mm());
    width_in_mm = info.extent_in_mm().width;
    height_in_mm = info.extent_in_mm().height;
    ASSERT_TRUE(info.has_recommended_device_pixel_ratio());
    dpr_x = info.recommended_device_pixel_ratio().x;
    dpr_y = info.recommended_device_pixel_ratio().y;
  });

  EXPECT_EQ(width_in_px, 777U);
  EXPECT_EQ(height_in_px, 555U);
  EXPECT_EQ(width_in_mm, 77U);
  EXPECT_EQ(height_in_mm, 55U);
  EXPECT_EQ(dpr_x, 1.f);
  EXPECT_EQ(dpr_y, 1.f);
}

TEST(SingletonDisplayService, DevicePixelRatioChange) {
  auto display = std::make_shared<Display>(
      0, 777, 555, 77, 55, std::vector<zx_pixel_format_t>{ZX_PIXEL_FORMAT_ARGB_8888});
  auto singleton = std::make_unique<SingletonDisplayService>(display);

  const float kDPRx = 1.25f;
  const float kDPRy = 1.25f;
  display->set_device_pixel_ratio({kDPRx, kDPRy});

  float dpr_x = 0.f;
  float dpr_y = 0.f;
  singleton->GetMetrics([&](::fuchsia::ui::display::singleton::Metrics info) {
    dpr_x = info.recommended_device_pixel_ratio().x;
    dpr_y = info.recommended_device_pixel_ratio().y;
  });

  EXPECT_EQ(dpr_x, kDPRx);
  EXPECT_EQ(dpr_y, kDPRy);
}

}  // namespace test
}  // namespace display
}  // namespace scenic_impl
