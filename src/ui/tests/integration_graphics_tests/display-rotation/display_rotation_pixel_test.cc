// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

    // Get display's width and height.
    auto [width, height] = ui_test_manager_->GetDisplayDimensions();

    display_width_ = width;
    display_height_ = height;
    FX_LOGS(INFO) << "Got display_width = " << display_width_
                  << " and display_height = " << display_height_;
  }

  ui_testing::Screenshot TakeScreenshot() { return ui_test_manager_->TakeScreenshot(); }

  // Validates that the content present in |screenshot| matches the content of
  // |ui_testing::TestView::ContentType::COORDINATE_GRID|.
  static void AssertScreenshot(const ui_testing::Screenshot& screenshot) {
    // Check pixel content at all four corners.
    EXPECT_EQ(screenshot.GetPixelAt(0, 0), ui_testing::Screenshot::kBlack);  // Top left
    EXPECT_EQ(screenshot.GetPixelAt(0, screenshot.height() - 1),
              ui_testing::Screenshot::kBlue);  // Bottom left
    EXPECT_EQ(screenshot.GetPixelAt(screenshot.width() - 1, 0),
              ui_testing::Screenshot::kRed);  // Top right
    EXPECT_EQ(screenshot.GetPixelAt(screenshot.width() - 1, screenshot.height() - 1),
              ui_testing::Screenshot::kMagenta);  // Bottom right

    // Check pixel content at center of each rectangle.
    EXPECT_EQ(screenshot.GetPixelAt(screenshot.width() / 4, screenshot.height() / 4),
              ui_testing::Screenshot::kBlack);  // Top left
    EXPECT_EQ(screenshot.GetPixelAt(screenshot.width() / 4, (3 * screenshot.height()) / 4),
              ui_testing::Screenshot::kBlue);  // Bottom left
    EXPECT_EQ(screenshot.GetPixelAt((3 * screenshot.width()) / 4, screenshot.height() / 4),
              ui_testing::Screenshot::kRed);  // Top right
    EXPECT_EQ(screenshot.GetPixelAt((3 * screenshot.width()) / 4, (3 * screenshot.height()) / 4),
              ui_testing::Screenshot::kMagenta);  // Bottom right
    EXPECT_EQ(screenshot.GetPixelAt(screenshot.width() / 2, screenshot.height() / 2),
              ui_testing::Screenshot::kGreen);  // Center
  }

  float ClientViewScaleFactor() { return ui_test_manager_->ClientViewScaleFactor(); }

  uint64_t display_height_ = 0.f;
  uint64_t display_width_ = 0.f;
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

  // The width and height of the screenshot should be the same as that of the display for landscape
  // orientation.
  ASSERT_EQ(data.width(), display_width_);
  ASSERT_EQ(data.height(), display_height_);

  EXPECT_EQ(test_view_->width(),
            static_cast<uint64_t>(static_cast<float>(data.width()) / scale_factor));
  EXPECT_EQ(test_view_->height(),
            static_cast<uint64_t>(static_cast<float>(data.height()) / scale_factor));

  // The content of the screenshot should be independent of the display's orientation.
  AssertScreenshot(data);
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
  ASSERT_EQ(data.width(), display_height_);
  ASSERT_EQ(data.height(), display_width_);

  EXPECT_EQ(test_view_->width(),
            static_cast<uint64_t>(static_cast<float>(data.width()) / scale_factor));
  EXPECT_EQ(test_view_->height(),
            static_cast<uint64_t>(static_cast<float>(data.height()) / scale_factor));

  // The content of the screenshot should be independent of the display's orientation.
  AssertScreenshot(data);
}

}  // namespace integration_tests
