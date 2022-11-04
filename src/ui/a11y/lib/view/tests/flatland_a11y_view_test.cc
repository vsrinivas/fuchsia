// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/scene/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/view/flatland_accessibility_view.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"
#include "src/ui/testing/util/flatland_test_view.h"

namespace accessibility_test {
namespace {

using component_testing::ChildRef;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Route;

constexpr auto kViewProvider = "view-provider";

// This test fixture sets up a test realm with scenic and a11y manager.
// The test fixture mocks the "scene owner" portion of the handshake by creating
// a flatland display, attaching the a11y viewport as its content, and
// requesting the a11y manager to insert its view. Finally, the test fixture
// inserts a proxy view as a child of the proxy viewport the a11y manager
// creates. If the proxy view is attached to the scene, the a11y manager must
// have performed its portion of the handshake correctly. The final topology
// should be:
//
//      flatland display (owned by test fixture)
//            |
//      a11y view transform (owned by a11y manager)
//            |
//      proxy viewport transform (owned by a11y manager)
//            |
//       proxy view transform (owned by test fixture)
class FlatlandAccessibilityViewTest : public gtest::RealLoopFixture {
 public:
  FlatlandAccessibilityViewTest() = default;
  ~FlatlandAccessibilityViewTest() override = default;

  void SetUp() override {
    // Don't specify a scene_owner to force a scenic-only realm.
    ui_testing::UITestRealm::Config config;
    config.use_flatland = true;
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_,
                                    fuchsia::ui::composition::Flatland::Name_};
    config.exposed_client_services = {fuchsia::accessibility::scene::Provider::Name_,
                                      fuchsia::ui::app::ViewProvider::Name_};
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    // Build realm.
    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<component_testing::Realm>(ui_test_manager_->AddSubrealm());

    // Add a test view provider.
    test_view_ = std::make_unique<ui_testing::FlatlandTestView>(
        dispatcher(), /* content = */ ui_testing::TestView::ContentType::COORDINATE_GRID);
    realm_->AddLocalChild(kViewProvider, test_view_.get());
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                           .source = ChildRef{kViewProvider},
                           .targets = {ParentRef()}});
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::composition::Flatland::Name_}},
                           .source = ParentRef(),
                           .targets = {ChildRef{kViewProvider}}});

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();

    // Get display's width and height.
    auto [width, height] = ui_test_manager_->GetDisplayDimensions();

    display_width_ = width;
    display_height_ = height;
    FX_LOGS(INFO) << "Got display_width = " << display_width_
                  << " and display_height = " << display_height_;

    SetUpScene();
  }

  void SetUpScene() {
    flatland_display_ =
        realm_exposed_services()->Connect<fuchsia::ui::composition::FlatlandDisplay>();
    auto proxy_flatland = realm_exposed_services()->Connect<fuchsia::ui::composition::Flatland>();

    a11y_view_ = std::make_unique<a11y::FlatlandAccessibilityView>(
        realm_exposed_services()->Connect<fuchsia::ui::composition::Flatland>(),
        realm_exposed_services()->Connect<fuchsia::ui::composition::Flatland>());

    // Set up the display, and add the a11y viewport as the display content.
    // Note that we don't need an extra view between the display and the a11y
    // view; we're only concerned if the a11y view creates its view and the
    // proxy viewport correctly.
    fidl::InterfacePtr<fuchsia::ui::composition::ChildViewWatcher> child_view_watcher;
    auto [a11y_view_token, a11y_viewport_token] = scenic::ViewCreationTokenPair::New();
    flatland_display_->SetContent(std::move(a11y_viewport_token), child_view_watcher.NewRequest());

    // Create the proxy view/viewport tokens.
    auto [proxy_view_token, proxy_viewport_token] = scenic::ViewCreationTokenPair::New();

    // Request for the a11y manager to insert its view.
    a11y_view_->CreateView(std::move(a11y_view_token), std::move(proxy_viewport_token));

    // Create the test view.
    fuchsia::ui::app::CreateView2Args args;
    args.set_view_creation_token(std::move(proxy_view_token));
    auto view_provider = realm_exposed_services()->Connect<fuchsia::ui::app::ViewProvider>();
    view_provider->CreateView2(std::move(args));

    // Run until the proxy view has been attached to the scene, which can only
    // happen if the a11y manager has correctly inserted its view.
    FX_LOGS(INFO) << "Waiting for client view to render";
    RunLoopUntil([this]() {
      auto test_view_ref_koid = test_view_->GetViewRefKoid();
      return test_view_ref_koid.has_value() &&
             ui_test_manager_->ViewIsRendering(*test_view_ref_koid);
    });

    // Verify that the a11y view is ready.
    a11y_view_->add_scene_ready_callback([this]() {
      QuitLoop();
      return true;
    });
    FX_LOGS(INFO) << "Waiting for a11y view to be ready";
    RunLoop();

    // Verify that the a11y view has its ViewRef.
    EXPECT_TRUE(a11y_view_->view_ref().has_value());
  }

  sys::ServiceDirectory* realm_exposed_services() { return realm_exposed_services_.get(); }

 protected:
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<component_testing::Realm> realm_;
  std::unique_ptr<ui_testing::TestView> test_view_;
  std::unique_ptr<a11y::FlatlandAccessibilityView> a11y_view_;
  fuchsia::ui::composition::FlatlandDisplayPtr flatland_display_;

  uint64_t display_height_ = 0.f;
  uint64_t display_width_ = 0.f;
};

TEST_F(FlatlandAccessibilityViewTest, TestSceneConnected) {
  auto data = ui_test_manager_->TakeScreenshot();

  // Spot-check the pixels at the center of each quadrant and the corners of the display.
  EXPECT_EQ(data.GetPixelAt(data.width() / 4, data.height() / 4), ui_testing::Screenshot::kBlack);
  EXPECT_EQ(data.GetPixelAt(data.width() / 4, 3 * data.height() / 4),
            ui_testing::Screenshot::kBlue);
  EXPECT_EQ(data.GetPixelAt(3 * data.width() / 4, data.height() / 4), ui_testing::Screenshot::kRed);
  EXPECT_EQ(data.GetPixelAt(3 * data.width() / 4, 3 * data.height() / 4),
            ui_testing::Screenshot::kMagenta);

  EXPECT_EQ(data.GetPixelAt(0, 0), ui_testing::Screenshot::kBlack);
  EXPECT_EQ(data.GetPixelAt(0, data.height() - 1), ui_testing::Screenshot::kBlue);
  EXPECT_EQ(data.GetPixelAt(data.width() - 1, 0), ui_testing::Screenshot::kRed);
  EXPECT_EQ(data.GetPixelAt(data.width() - 1, data.height() - 1), ui_testing::Screenshot::kMagenta);

  // Verify alignment based on pixel histogram data.

  // Width and height of the rectangle in the center is |width|/4 and
  // |height|/4.
  const auto expected_green_pixels = (display_width_ / 4) * (display_height_ / 4);

  // The number of pixels inside each quadrant would be |width|/2 * |height|/2.
  // Since the central rectangle would cover equal areas in each quadrant, we subtract
  // |expected_green_pixels|/4 from each quadrants to get the pixel for the quadrant's color.
  const auto expected_black_pixels =
      (display_width_ / 2) * (display_height_ / 2) - (expected_green_pixels / 4);

  const auto expected_blue_pixels = expected_black_pixels,
             expected_red_pixels = expected_black_pixels,
             expected_magenta_pixels = expected_black_pixels;

  auto histogram = data.Histogram();

  EXPECT_EQ(histogram[ui_testing::Screenshot::kBlack], expected_black_pixels);
  EXPECT_EQ(histogram[ui_testing::Screenshot::kBlue], expected_blue_pixels);
  EXPECT_EQ(histogram[ui_testing::Screenshot::kRed], expected_red_pixels);
  EXPECT_EQ(histogram[ui_testing::Screenshot::kMagenta], expected_magenta_pixels);
  EXPECT_EQ(histogram[ui_testing::Screenshot::kGreen], expected_green_pixels);
}

TEST_F(FlatlandAccessibilityViewTest, TestMagnification) {
  a11y_view_->SetMagnificationTransform(/* scale = */ 4, /* translation_x = */ -1.25f,
                                        /* translation_y = */ 1.5f, [this]() { QuitLoop(); });
  RunLoop();

  auto data = ui_test_manager_->TakeScreenshot();

  // Spot-check the pixels at the center of each quadrant and the corners of the display.
  EXPECT_EQ(data.GetPixelAt(data.width() / 4, data.height() / 4), ui_testing::Screenshot::kRed);
  EXPECT_EQ(data.GetPixelAt(data.width() / 4, 3 * data.height() / 4),
            ui_testing::Screenshot::kGreen);
  EXPECT_EQ(data.GetPixelAt(3 * data.width() / 4, data.height() / 4), ui_testing::Screenshot::kRed);
  EXPECT_EQ(data.GetPixelAt(3 * data.width() / 4, 3 * data.height() / 4),
            ui_testing::Screenshot::kRed);

  EXPECT_EQ(data.GetPixelAt(0, 0), ui_testing::Screenshot::kRed);
  EXPECT_EQ(data.GetPixelAt(0, data.height() - 1), ui_testing::Screenshot::kGreen);
  EXPECT_EQ(data.GetPixelAt(data.width() - 1, 0), ui_testing::Screenshot::kRed);
  EXPECT_EQ(data.GetPixelAt(data.width() - 1, data.height() - 1), ui_testing::Screenshot::kRed);

  // Verify alignment based on pixel histogram data.

  // The translation specified in `SetMagnificationTransform` is applied to the
  // scaled NDC space, so for the given scale of 4, each axis spans [-4, 4]. The
  // "viewport" will be the portion of the scaled space in [-1, 1] on each
  // axis. After the scale of 4 is applied, the test view's green panel will
  // exactly match the size and position of the "viewport". Once the translation
  // is applied, the green panel will move to the bottom-left corner of the
  // viewport, covering 3/4 the width and 1/2 the height of that quadrant. The
  // expected number of green pixels is therefore 3/4 * 1/2 * 1/4 * num_pixels.
  // The rest of the pixels should be red.
  const auto num_pixels = display_width_ * display_height_;
  const auto expected_green_pixels = 3 * num_pixels / 32;
  const auto expected_red_pixels = num_pixels - expected_green_pixels;

  auto histogram = data.Histogram();

  EXPECT_EQ(histogram[ui_testing::Screenshot::kBlack], 0u);
  EXPECT_EQ(histogram[ui_testing::Screenshot::kBlue], 0u);
  EXPECT_EQ(histogram[ui_testing::Screenshot::kRed], expected_red_pixels);
  EXPECT_EQ(histogram[ui_testing::Screenshot::kMagenta], 0u);
  EXPECT_EQ(histogram[ui_testing::Screenshot::kGreen], expected_green_pixels);
}

TEST_F(FlatlandAccessibilityViewTest, TestHighlight) {
  // The color used for a11y highlights.
  const ui_testing::Pixel kHighlightColor =
      ui_testing::Pixel::from_linear_brga(0x57, 0x00, 0xF5, 0xFF);

  // Draw an a11y highlight around a rect in the middle of the screen.
  const int left = static_cast<int>(display_width_ * 1 / 4);
  const int top = static_cast<int>(display_height_ * 1 / 4);
  const int right = static_cast<int>(display_width_ * 3 / 4);
  const int bottom = static_cast<int>(display_height_ * 3 / 4);
  a11y_view_->DrawHighlight({left, top}, {right, bottom}, [this]() { QuitLoop(); });
  RunLoop();

  auto data = ui_test_manager_->TakeScreenshot();

  EXPECT_EQ(data.GetPixelAt(data.width() * 1 / 2, data.height() * 1 / 2),
            ui_testing::Screenshot::kGreen)
      << "center pixel should be green";

  // Check a horizontal slice.
  {
    const int middle = static_cast<int>(display_height_ / 2);
    // Example: If left=200, the pixels in the columns in the closed range [197, 202] should be
    // drawn.
    EXPECT_NE(data.GetPixelAt(left - 4, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left - 3, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left - 2, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left - 1, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left + 0, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left + 1, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left + 2, middle), kHighlightColor);
    EXPECT_NE(data.GetPixelAt(left + 3, middle), kHighlightColor);

    // And if right=600, the pixels in the columns in the closed range [597, 602] should be drawn.
    EXPECT_NE(data.GetPixelAt(right - 4, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right - 3, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right - 2, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right - 1, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right + 0, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right + 1, middle), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right + 2, middle), kHighlightColor);
    EXPECT_NE(data.GetPixelAt(right + 3, middle), kHighlightColor);
  }

  // Check a vertical slice.
  {
    const int middle = static_cast<int>(display_width_ / 2);
    // If top=200, the pixels in the rows in the closed range [197, 202] should be drawn.
    EXPECT_NE(data.GetPixelAt(middle, top - 4), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, top - 3), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, top - 2), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, top - 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, top + 0), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, top + 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, top + 2), kHighlightColor);
    EXPECT_NE(data.GetPixelAt(middle, top + 3), kHighlightColor);

    // And if bottom=600, the pixels in the rows in the closed range [597, 602] should be drawn.
    EXPECT_NE(data.GetPixelAt(middle, bottom - 4), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, bottom - 3), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, bottom - 2), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, bottom - 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, bottom + 0), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, bottom + 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(middle, bottom + 2), kHighlightColor);
    EXPECT_NE(data.GetPixelAt(middle, bottom + 3), kHighlightColor);
  }

  // Check the upper left corner.
  {
    EXPECT_NE(data.GetPixelAt(left - 4, top - 4), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left - 3, top - 3), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left - 2, top - 2), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left - 1, top - 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left + 0, top + 0), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left + 1, top + 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left + 2, top + 2), kHighlightColor);
    EXPECT_NE(data.GetPixelAt(left + 3, top + 3), kHighlightColor);
  }

  // Check the bottom right corner.
  {
    EXPECT_NE(data.GetPixelAt(right - 4, bottom - 4), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right - 3, bottom - 3), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right - 2, bottom - 2), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right - 1, bottom - 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right + 0, bottom + 0), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right + 1, bottom + 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right + 2, bottom + 2), kHighlightColor);
    EXPECT_NE(data.GetPixelAt(right + 3, bottom + 3), kHighlightColor);
  }

  // Check the upper right corner.
  {
    EXPECT_NE(data.GetPixelAt(right + 3, top - 4), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right + 2, top - 3), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right + 1, top - 2), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right + 0, top - 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right - 1, top + 0), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right - 2, top + 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(right - 3, top + 2), kHighlightColor);
    EXPECT_NE(data.GetPixelAt(right - 4, top + 3), kHighlightColor);
  }

  // Check the bottom left corner.
  {
    EXPECT_NE(data.GetPixelAt(left + 3, bottom - 4), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left + 2, bottom - 3), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left + 1, bottom - 2), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left + 0, bottom - 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left - 1, bottom + 0), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left - 2, bottom + 1), kHighlightColor);
    EXPECT_EQ(data.GetPixelAt(left - 3, bottom + 2), kHighlightColor);
    EXPECT_NE(data.GetPixelAt(left - 4, bottom + 2), kHighlightColor);
  }
}

TEST_F(FlatlandAccessibilityViewTest, TestClearHighlight) {
  {
    auto data = ui_test_manager_->TakeScreenshot();
    EXPECT_EQ(data.GetPixelAt(data.width() * 3 / 8, data.height() * 3 / 8),
              ui_testing::Screenshot::kGreen)
        << "pixel at upper left of highlight rect should be green";
  }

  // Draw an a11y highlight.
  const int left = static_cast<int>(display_width_ * 3 / 8);
  const int top = static_cast<int>(display_height_ * 3 / 8);
  const int right = static_cast<int>(display_width_ * 5 / 8);
  const int bottom = static_cast<int>(display_height_ * 5 / 8);
  a11y_view_->DrawHighlight({left, top}, {right, bottom}, [this]() { QuitLoop(); });
  RunLoop();

  {
    auto data = ui_test_manager_->TakeScreenshot();
    EXPECT_NE(data.GetPixelAt(data.width() * 3 / 8, data.height() * 3 / 8),
              ui_testing::Screenshot::kGreen)
        << "pixel at upper left of highlight rect should not be green";
  }

  // Clear the a11y highlight.
  a11y_view_->ClearHighlight([this]() { QuitLoop(); });
  RunLoop();

  {
    auto data = ui_test_manager_->TakeScreenshot();
    EXPECT_EQ(data.GetPixelAt(data.width() * 3 / 8, data.height() * 3 / 8),
              ui_testing::Screenshot::kGreen)
        << "pixel at upper left of highlight rect should be green again";
  }
}

// Make sure that calling ClearHighlight and DrawHighlight multiple times
// doesn't cause a Flatland error.
TEST_F(FlatlandAccessibilityViewTest, MultipleCallsDontCrash) {
  a11y_view_->ClearHighlight([this]() { QuitLoop(); });
  RunLoop();

  a11y_view_->ClearHighlight([this]() { QuitLoop(); });
  RunLoop();

  const int left = static_cast<int>(display_width_ * 1 / 4);
  const int top = static_cast<int>(display_height_ * 1 / 4);
  const int right = static_cast<int>(display_width_ * 3 / 4);
  const int bottom = static_cast<int>(display_height_ * 3 / 4);
  a11y_view_->DrawHighlight({left, top}, {right, bottom}, [this]() { QuitLoop(); });
  RunLoop();

  a11y_view_->DrawHighlight({left, top}, {right, bottom}, [this]() { QuitLoop(); });
  RunLoop();

  a11y_view_->ClearHighlight([this]() { QuitLoop(); });
  RunLoop();

  a11y_view_->ClearHighlight([this]() { QuitLoop(); });
  RunLoop();
}

}  // namespace
}  // namespace accessibility_test
