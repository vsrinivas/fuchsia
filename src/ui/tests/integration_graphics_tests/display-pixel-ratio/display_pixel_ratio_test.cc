// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/display/singleton/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <test/accessibility/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"
#include "src/ui/testing/util/gfx_test_view.h"

namespace integration_tests {
namespace {

constexpr auto kViewProvider = "view-provider";
constexpr float kEpsilon = 0.005f;

// Colors at specified locations in the test view.
constexpr uint8_t kUpperLeftColor[] = {0, 0, 0, 255};
constexpr uint8_t kUpperRightColor[] = {0, 0, 255, 255};
constexpr uint8_t kLowerLeftColor[] = {255, 0, 0, 255};
constexpr uint8_t kLowerRightColor[] = {255, 0, 255, 255};
constexpr uint8_t kCenterColor[] = {0, 255, 0, 255};

constexpr uint32_t kBytesPerPixel = 4;

struct DisplayProperties {
  // Arbitrarily-chosen value.
  float display_pixel_density = 0.f;

  // This is the scale value that should result from a pixel density of |display_pixel_density|.
  // Calculated in DisplayMetrics
  // (https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/scene_management/src/display_metrics.rs).
  float expected_scale = 0.f;

  DisplayProperties(float display_pixel_density, float expected_scale)
      : display_pixel_density(display_pixel_density), expected_scale(expected_scale) {}
};

// Returns a list of display pixel densities with its corresponding expected scale value.
static std::vector<DisplayProperties> GetPixelDensityToScaleValues() {
  const float kAstroDisplayPixelDensity = 4.1668f;
  const float kAstroExpectedScale = 1.2549f;
  const float kSherlockDisplayPixelDensity = 5.2011f;
  const float kSherlockExpectedScale = 1.f;

  std::vector<DisplayProperties> pixel_density;
  pixel_density.emplace_back(kAstroDisplayPixelDensity, kAstroExpectedScale);
  pixel_density.emplace_back(kSherlockDisplayPixelDensity, kSherlockExpectedScale);
  return pixel_density;
}

}  // namespace

using component_testing::ChildRef;
using component_testing::Directory;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

// This test verifies that Root Presenter and Scene Manager propagate
// 'config/data/display_pixel_density' correctly.
class DisplayPixelRatioTest
    : public gtest::RealLoopFixture,
      public ::testing::WithParamInterface<
          std::tuple<ui_testing::UITestRealm::SceneOwnerType, DisplayProperties>> {
 protected:
  // |testing::Test|
  void SetUp() override {
    ui_testing::UITestRealm::Config config;
    config.scene_owner = std::get<0>(GetParam());  // scene owner.
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_};
    config.display_pixel_density = std::get<1>(GetParam()).display_pixel_density;
    config.display_usage = "close";
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
  }

  float ClientViewScaleFactor() { return ui_test_manager_->ClientViewScaleFactor(); }

  // Returns an array of size |display_width_ * display_height_ * kBytesPerPixel| representing the
  // RGBA values of the pixels on the screen. Every |kBytesPerPixel| bytes represent the RGBA values
  // of a pixel. Skip |kBytesPerPixel| bytes to get to the RGBA values of the next pixel.
  //
  // Example:-
  // auto data = TakeScreenshot();
  // data[0-4] -> RGBA of pixel 0.
  // data[4-8] -> RGBA pf pixel 1.
  uint8_t* TakeScreenshot() {
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

    uint8_t* vmo_host = nullptr;
    auto status = zx::vmar::root_self()->map(ZX_VM_PERM_READ, /*vmar_offset*/ 0, response.vmo(),
                                             /*vmo_offset*/ 0, vmo_size,
                                             reinterpret_cast<uintptr_t*>(&vmo_host));
    FX_DCHECK(status == ZX_OK);

    return vmo_host;
  }

  void ExpectEqualPixels(const uint8_t* pixel1, const uint8_t* pixel2) {
    EXPECT_TRUE(memcmp(pixel1, pixel2, kBytesPerPixel) == 0);
  }

  // Returns the RGBA values of the pixel at (x * display_width_,y * display_height_) coordinate.
  // (x,y) lies in [0,1].
  uint8_t* ColorAt(float x, float y, uint8_t* data) {
    FX_DCHECK(x >= 0 && y >= 0 && x <= 1.f && y <= 1.f);
    if (x == 1.f) {
      x -= kEpsilon;  // ensure upper bound becomes a reasonable index value.
    }
    if (y == 1.f) {
      y -= kEpsilon;  // ensure upper bound becomes a reasonable index value.
    }

    const auto x_coor = static_cast<size_t>(x * display_width_);
    const auto y_coor = static_cast<size_t>(y * display_height_);
    return &data[kBytesPerPixel * (y_coor * static_cast<size_t>(display_width_) + x_coor)];
  }

  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuchsia::ui::composition::ScreenshotPtr screenshotter_;
  fuchsia::ui::display::singleton::InfoPtr display_info_;
  std::unique_ptr<ui_testing::TestView> test_view_;
  double display_width_ = 0;
  double display_height_ = 0;

 private:
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;

  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<Realm> realm_;
};

INSTANTIATE_TEST_SUITE_P(
    DisplayPixelRatioTestWithParams, DisplayPixelRatioTest,
    testing::Combine(::testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                       ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER),
                     testing::ValuesIn(GetPixelDensityToScaleValues())));

// This test leverage the coordinate test view to ensure that display pixel ratio is working
// properly.
// ___________________________________
// |                |                |
// |     BLACK      |        BLUE    |
// |           _____|_____           |
// |___________|  GREEN  |___________|
// |           |_________|           |
// |                |                |
// |      RED       |     MAGENTA    |
// |________________|________________|
TEST_P(DisplayPixelRatioTest, TestScale) {
  auto expected_scale = std::get<1>(GetParam()).expected_scale;
  EXPECT_NEAR(ClientViewScaleFactor(), 1.0f / expected_scale, kEpsilon);

  std::optional<fuchsia::ui::display::singleton::Metrics> info;
  display_info_->GetMetrics([&info](fuchsia::ui::display::singleton::Metrics display_info) {
    info = std::move(display_info);
  });
  RunLoopUntil([&info] { return info.has_value(); });

  display_width_ = info->extent_in_px().width;
  display_height_ = info->extent_in_px().height;
  FX_LOGS(INFO) << "Got display_width = " << display_width_
                << " and display_height = " << display_height_;

  EXPECT_NEAR(test_view_->width() / display_width_, expected_scale, kEpsilon);
  EXPECT_NEAR(test_view_->height() / display_height_, expected_scale, kEpsilon);

  // The drawn content should cover the screen's display.
  auto data = TakeScreenshot();

  // Check pixel content at all four corners.
  ExpectEqualPixels(ColorAt(0.f, 0.f, data), kUpperLeftColor);
  ExpectEqualPixels(ColorAt(0.f, 1.f, data), kLowerLeftColor);
  ExpectEqualPixels(ColorAt(1.f, 0.f, data), kUpperRightColor);
  ExpectEqualPixels(ColorAt(1.f, 1.f, data), kLowerRightColor);

  // Check pixel content at center of each rectangle.
  ExpectEqualPixels(ColorAt(0.25f, 0.25f, data), kUpperLeftColor);
  ExpectEqualPixels(ColorAt(0.25f, 0.75f, data), kLowerLeftColor);
  ExpectEqualPixels(ColorAt(0.75f, 0.25f, data), kUpperRightColor);
  ExpectEqualPixels(ColorAt(0.75f, 0.75f, data), kLowerRightColor);
  ExpectEqualPixels(ColorAt(0.5f, 0.5f, data), kCenterColor);
}

}  // namespace integration_tests
