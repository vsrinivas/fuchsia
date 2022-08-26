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
#include <test/accessibility/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"
#include "src/ui/testing/util/gfx_test_view.h"

namespace integration_tests {

using component_testing::ChildRef;
using component_testing::Directory;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

constexpr auto kViewProvider = "view-provider";
constexpr float kEpsilon = 0.01f;

// Arbitrarily-chosen value.
constexpr float kDisplayPixelDensity = 4.1668f;
// This is the scale value that should result from a pixel density of kDisplayPixelDensity.
// Calculated in DisplayMetrics
// (https://source.corp.google.com/fuchsia/src/ui/lib/scene_management/src/display_metrics.rs).
constexpr float kExpectedScale = 1.2549f;

// This test verifies that Root Presenter and Scene Manager propagate
// 'config/data/display_pixel_density' correctly.
class DisplayPixelRatioTest
    : public gtest::RealLoopFixture,
      public ::testing::WithParamInterface<ui_testing::UITestRealm::SceneOwnerType> {
 public:
  fuchsia::ui::scenic::ScenicPtr scenic;
  std::unique_ptr<ui_testing::TestView> test_view;

 protected:
  // |testing::Test|
  void SetUp() override {
    ui_testing::UITestRealm::Config config;
    config.scene_owner = GetParam();
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_};
    config.display_pixel_density = kDisplayPixelDensity;
    config.display_usage = "close";
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    // Build realm.
    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());

    // Add a test view provider.
    test_view = std::make_unique<ui_testing::GfxTestView>(
        dispatcher(), /* content = */ ui_testing::TestView::ContentType::DEFAULT);
    realm_->AddLocalChild(kViewProvider, test_view.get());
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

    scenic = realm_exposed_services_->Connect<fuchsia::ui::scenic::Scenic>();
  }

  float ClientViewScaleFactor() { return ui_test_manager_->ClientViewScaleFactor(); }

 private:
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;

  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<Realm> realm_;
};

INSTANTIATE_TEST_SUITE_P(DisplayPixelRatioTestWithParams, DisplayPixelRatioTest,
                         ::testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                           ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER));

TEST_P(DisplayPixelRatioTest, TestScale) {
  EXPECT_NEAR(ClientViewScaleFactor(), 1.0f / kExpectedScale, kEpsilon);

  scenic->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
    const double display_width_ = display_info.width_in_px;
    const double display_height_ = display_info.height_in_px;
    FX_LOGS(INFO) << "Got display_width = " << display_width_
                  << " and display_height = " << display_height_;

    EXPECT_NEAR(test_view->width() / display_width_, kExpectedScale, kEpsilon);
    EXPECT_NEAR(test_view->height() / display_height_, kExpectedScale, kEpsilon);
    QuitLoop();
  });

  RunLoop();
}

}  // namespace integration_tests
