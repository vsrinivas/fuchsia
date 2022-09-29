// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/display/singleton/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"
#include "src/ui/testing/util/gfx_test_view.h"

namespace integration_tests {
namespace {

constexpr auto kViewProvider = "view-provider";

constexpr float kEpsilon = 0.005f;

constexpr uint64_t kBytesPerPixel = 4;

// BGRA format.
constexpr uint8_t kBlack[] = {0, 0, 0, 255};
constexpr uint8_t kBlue[] = {255, 0, 0, 255};
constexpr uint8_t kRed[] = {0, 0, 255, 255};
constexpr uint8_t kMagenta[] = {255, 0, 255, 255};
constexpr uint8_t kGreen[] = {0, 255, 0, 255};

}  // namespace

using component_testing::ChildRef;
using component_testing::Directory;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

// This test verifies that Root Presenter and Scene Manager propagate
// 'config/data/display_rotation' correctly.
class DisplayRotationPixelTestBase : public gtest::RealLoopFixture {
 protected:
  DisplayRotationPixelTestBase(ui_testing::UITestRealm::SceneOwnerType scene_owner, int rotation)
      : scene_owner_(scene_owner), rotation_(rotation) {}

  // |testing::Test|
  void SetUp() override {
    ui_testing::UITestRealm::Config config;
    config.scene_owner = scene_owner_;
    config.display_rotation = rotation_;
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_};
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    // Build realm.
    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());

    // Add a test view provider.
    test_view_ = std::make_unique<ui_testing::GfxTestView>(
        dispatcher(), /* content = */ ui_testing::TestView::ContentType::COORDINATE_GRID);
    realm_->AddLocalChild(kViewProvider, test_view_.get());
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                           .source = ChildRef{kViewProvider},
                           .targets = {ParentRef()}});
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                           .source = ParentRef(),
                           .targets = {ChildRef{kViewProvider}}});

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();

    // Attach view, and wait for it to render.
    ui_test_manager_->InitializeScene();
    RunLoopUntil([this]() { return ui_test_manager_->ClientViewIsRendering(); });

    scenic_ = realm_exposed_services_->Connect<fuchsia::ui::scenic::Scenic>();
    screenshotter_ = realm_exposed_services_->Connect<fuchsia::ui::composition::Screenshot>();
    display_info_ = realm_exposed_services_->Connect<fuchsia::ui::display::singleton::Info>();

    // Get display's width and height.
    std::optional<fuchsia::ui::display::singleton::Metrics> info;
    display_info_->GetMetrics([&info](fuchsia::ui::display::singleton::Metrics display_info) {
      info = std::move(display_info);
    });
    RunLoopUntil([&info] { return info.has_value(); });

    display_width_ = info->extent_in_px().width;
    display_height_ = info->extent_in_px().height;
    FX_LOGS(INFO) << "Got display_width = " << display_width_
                  << " and display_height = " << display_height_;
  }

  // Returns an array of size |display_width_ * display_height_ * kBytesPerPixel| representing the
  // BGRA values of the pixels on the screen. Every |kBytesPerPixel| bytes represent the BGRA values
  // of a pixel. Skip |kBytesPerPixel| bytes to get to the BGRA values of the next pixel.
  //
  // Example:-
  // auto data = TakeScreenshot();
  // data[0-4] -> BGRA of pixel 0.
  // data[4-8] -> BGRA pf pixel 1.
  std::vector<uint8_t> TakeScreenshot() {
    fuchsia::ui::composition::ScreenshotTakeRequest request;
    request.set_format(fuchsia::ui::composition::ScreenshotFormat::BGRA_RAW);

    std::optional<bool> has_completed;
    fuchsia::ui::composition::ScreenshotTakeResponse response;
    screenshotter_->Take(std::move(request), [&response, &has_completed](auto screenshot_response) {
      response = std::move(screenshot_response);
      has_completed = true;
    });

    RunLoopUntil([&has_completed] { return has_completed.has_value(); });
    EXPECT_TRUE(has_completed.value());

    uint64_t vmo_size;
    response.vmo().get_prop_content_size(&vmo_size);

    EXPECT_EQ(vmo_size, kBytesPerPixel * static_cast<uint64_t>(display_height_ * display_width_));

    uint8_t* vmo_host = nullptr;
    auto status = zx::vmar::root_self()->map(ZX_VM_PERM_READ, /*vmar_offset*/ 0, response.vmo(),
                                             /*vmo_offset*/ 0, vmo_size,
                                             reinterpret_cast<uintptr_t*>(&vmo_host));

    FX_DCHECK(status == ZX_OK);

    std::vector<uint8_t> screenshot;
    screenshot.resize(vmo_size);
    memcpy(screenshot.data(), vmo_host, vmo_size);

    // Unmap the pointer.
    uintptr_t address = reinterpret_cast<uintptr_t>(vmo_host);
    status = zx::vmar::root_self()->unmap(address, vmo_size);
    FX_DCHECK(status == ZX_OK);

    return screenshot;
  }

  static void ExpectEqualPixels(const uint8_t* pixel1, const uint8_t* pixel2) {
    EXPECT_TRUE(memcmp(pixel1, pixel2, kBytesPerPixel) == 0);
  }

  // Returns the BGRA values of the pixel at (x * width,y * height) coordinate.
  // (x,y) lies in [0,1].
  static uint8_t* ColorAt(float x, float y, std::vector<uint8_t>& data, float width, float height) {
    FX_DCHECK(x >= 0 && y >= 0 && x <= 1.f && y <= 1.f);
    if (x == 1.f) {
      x -= kEpsilon;  // ensure upper bound becomes a reasonable index value.
    }
    if (y == 1.f) {
      y -= kEpsilon;  // ensure upper bound becomes a reasonable index value.
    }

    const auto x_coor = static_cast<size_t>(x * width);
    const auto y_coor = static_cast<size_t>(y * height);
    return &data[kBytesPerPixel * (y_coor * static_cast<size_t>(width) + x_coor)];
  }

  // Validates that the content present in |screenshot| matches the content of
  // |ui_testing::TestView::ContentType::COORDINATE_GRID|.
  static void AssertScreenshot(std::vector<uint8_t>& screenshot, float width, float height) {
    // Check pixel content at all four corners.
    ExpectEqualPixels(ColorAt(0.f, 0.f, screenshot, width, height), kBlack);  // Top left corner
    ExpectEqualPixels(ColorAt(0.f, 1.f, screenshot, width, height), kBlue);   // Bottom left corner
    ExpectEqualPixels(ColorAt(1.f, 0.f, screenshot, width, height), kRed);    // Top right corner
    ExpectEqualPixels(ColorAt(1.f, 1.f, screenshot, width, height),
                      kMagenta);  // Bottom right corner

    // Check pixel content at center of each rectangle.
    ExpectEqualPixels(ColorAt(0.25f, 0.25f, screenshot, width, height),
                      kBlack);  // Top left quadrant
    ExpectEqualPixels(ColorAt(0.25f, 0.75f, screenshot, width, height),
                      kBlue);  // Bottom left quadrent
    ExpectEqualPixels(ColorAt(0.75f, 0.25f, screenshot, width, height),
                      kRed);  // Top right quadrant
    ExpectEqualPixels(ColorAt(0.75f, 0.75f, screenshot, width, height),
                      kMagenta);  // Bottom right quadrant
    ExpectEqualPixels(ColorAt(0.5f, 0.5f, screenshot, width, height), kGreen);  // Center
  }

  float ClientViewScaleFactor() { return ui_test_manager_->ClientViewScaleFactor(); }

  uint32_t display_height_ = 0.f;
  uint32_t display_width_ = 0.f;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuchsia::ui::composition::ScreenshotPtr screenshotter_;
  fuchsia::ui::display::singleton::InfoPtr display_info_;
  std::unique_ptr<ui_testing::TestView> test_view_;

 private:
  ui_testing::UITestRealm::SceneOwnerType scene_owner_ =
      ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER;
  int rotation_ = 0;
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<Realm> realm_;
};

class LandscapeModeTest : public DisplayRotationPixelTestBase,
                          public ::testing::WithParamInterface<
                              std::tuple<ui_testing::UITestRealm::SceneOwnerType, int>> {
 public:
  // The display is said to be in landscape mode when it is oriented horizontally i.e rotated by 0
  // or 180 degrees.
  static std::vector<int> GetDisplayRotation() { return {0, 180}; }

 protected:
  LandscapeModeTest()
      : DisplayRotationPixelTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}
};

INSTANTIATE_TEST_SUITE_P(
    DisplayRotationPixelTestWithParams, LandscapeModeTest,
    testing::Combine(::testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                       ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER),
                     testing::ValuesIn(LandscapeModeTest::GetDisplayRotation())));

// This test leverage the coordinate test view to ensure that display rotation is working
// properly.
// _____________DISPLAY_______________
// |                |                |
// |     BLACK      |        RED     |
// |           _____|_____           |
// |___________|  GREEN  |___________|
// |           |_________|           |
// |                |                |
// |      BLUE      |     MAGENTA    |
// |________________|________________|
//
// The display is in landscape mode. By landscape we mean that the user sees the drawn content
// as shown above (display being rotated horizontally). The screenshot taken shows how the content
// is seen by the user.
TEST_P(LandscapeModeTest, ValidContentTest) {
  auto data = TakeScreenshot();
  auto scale_factor = ClientViewScaleFactor();

  auto width = static_cast<float>(display_width_);
  auto height = static_cast<float>(display_height_);

  EXPECT_EQ(test_view_->width(), static_cast<uint64_t>(width / scale_factor));
  EXPECT_EQ(test_view_->height(), static_cast<uint64_t>(height / scale_factor));

  // The content of the screenshot should be independent of the display's orientation.
  AssertScreenshot(data, width, height);
}

class PortraitModeTest : public DisplayRotationPixelTestBase,
                         public ::testing::WithParamInterface<
                             std::tuple<ui_testing::UITestRealm::SceneOwnerType, int>> {
 public:
  // The display is said to be in portrait mode when it is oriented vertically i.e rotated by 90 or
  // 270 degrees.
  static std::vector<int> GetDisplayRotation() { return {90, 270}; }

 protected:
  PortraitModeTest()
      : DisplayRotationPixelTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}
};

INSTANTIATE_TEST_SUITE_P(
    DisplayRotationPixelTestWithParams, PortraitModeTest,
    testing::Combine(::testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                       ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER),
                     testing::ValuesIn(PortraitModeTest::GetDisplayRotation())));

// This test leverage the coordinate test view to ensure that display rotation is working
// properly.
//  _____________________
// |          |          |
// |          |          |
// |          |          |D
// |  BLACK   |   RED    |I
// |        __|__        |S
// |       |     |       |P
// |-------|GREEN|--------L
// |       |     |       |A
// |       |__ __|       |Y
// |          |          |
// |  BLUE    |  MAGENTA |
// |          |          |
// |          |          |
//  _____________________
//
// The display is in portrait mode. By portrait we mean that the user sees the drawn content
// as shown above (display being rotated vertically). The screenshot taken shows how the content
// is seen by the user.
TEST_P(PortraitModeTest, ValidContentTest) {
  auto data = TakeScreenshot();
  auto scale_factor = ClientViewScaleFactor();

  // The width and height are flipped because the display is in portrait mode.
  auto width = static_cast<float>(display_height_);
  auto height = static_cast<float>(display_width_);

  EXPECT_EQ(test_view_->width(), static_cast<uint64_t>(width / scale_factor));
  EXPECT_EQ(test_view_->height(), static_cast<uint64_t>(height / scale_factor));

  // The content of the screenshot should be independent of the display's orientation.
  AssertScreenshot(data, width, height);
}

}  // namespace integration_tests
