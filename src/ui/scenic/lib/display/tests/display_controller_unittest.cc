// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include "src/ui/scenic/lib/display/display_manager2.h"
#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"

namespace scenic_impl {
namespace display {
namespace test {

class DisplayControllerTest : public gtest::TestLoopFixture {};

TEST_F(DisplayControllerTest, Display2Test) {
  const uint64_t kDisplayId = 2;
  const fuchsia::hardware::display::Mode kDisplayMode = {
      .horizontal_resolution = 1024, .vertical_resolution = 800, .refresh_rate_e2 = 60, .flags = 0};
  const zx_pixel_format_t kPixelFormat = ZX_PIXEL_FORMAT_ARGB_8888;

  Display2 display(kDisplayId, /*display_modes=*/{kDisplayMode}, /*pixel_format=*/{kPixelFormat});

  EXPECT_EQ(kDisplayId, display.display_id());
  EXPECT_TRUE(fidl::Equals(kDisplayMode, display.display_modes()[0]));
  EXPECT_EQ(kPixelFormat, display.pixel_formats()[0]);

  display.OnVsync(zx::time(1), {1});
  bool invoked_vsync_callback = false;
  display.set_on_vsync_callback([&](zx::time timestamp, const std::vector<uint64_t>& images) {
    invoked_vsync_callback = true;
    EXPECT_EQ(zx::time(2), timestamp);
    EXPECT_EQ(1u, images.size());
    EXPECT_EQ(2u, images[0]);
  });
  EXPECT_FALSE(invoked_vsync_callback);
  display.OnVsync(zx::time(2), {2});
  EXPECT_TRUE(invoked_vsync_callback);
}

TEST_F(DisplayControllerTest, DisplayControllerTest) {
  DisplayControllerObjects display_controller_objs = CreateMockDisplayController();

  const uint64_t kDisplayId1 = 1;
  const uint64_t kDisplayId2 = 2;
  const fuchsia::hardware::display::Mode kDisplayMode = {
      .horizontal_resolution = 1024, .vertical_resolution = 800, .refresh_rate_e2 = 60, .flags = 0};
  const zx_pixel_format_t kPixelFormat = ZX_PIXEL_FORMAT_ARGB_8888;

  Display2 display1(kDisplayId1, {kDisplayMode}, {kPixelFormat});
  Display2 display2(kDisplayId2, {kDisplayMode}, {kPixelFormat});

  std::vector<Display2> displays;
  displays.push_back(std::move(display1));
  DisplayController dc(std::move(displays), display_controller_objs.interface_ptr);

  EXPECT_EQ(display_controller_objs.interface_ptr.get(), dc.controller().get());

  EXPECT_EQ(1u, dc.displays()->size());
  EXPECT_EQ(kDisplayId1, dc.displays()->at(0).display_id());

  bool display_removed = false;
  dc.set_on_display_removed_callback([&](uint64_t display_id) {
    display_removed = true;
    EXPECT_EQ(kDisplayId1, display_id);
  });

  bool display_added = false;
  dc.set_on_display_added_callback([&](Display2* display) {
    display_added = true;
    EXPECT_EQ(kDisplayId2, display->display_id());
  });

  dc.AddDisplay(std::move(display2));

  EXPECT_TRUE(display_added);
  EXPECT_EQ(2u, dc.displays()->size());
  EXPECT_EQ(kDisplayId2, dc.displays()->at(1).display_id());

  dc.RemoveDisplay(kDisplayId1);
  EXPECT_EQ(1u, dc.displays()->size());
  EXPECT_EQ(kDisplayId2, dc.displays()->at(0).display_id());
}

}  // namespace test
}  // namespace display
}  // namespace scenic_impl
