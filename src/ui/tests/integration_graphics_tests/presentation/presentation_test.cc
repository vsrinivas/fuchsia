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
#include "src/ui/testing/views/color.h"

namespace integration_tests {

using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

constexpr auto kViewProvider = "view-provider";

// Colors at specified locations in the test view.
constexpr scenic::Color kUpperLeftColor = {0, 0, 0, 255};
constexpr scenic::Color kUpperRightColor = {0, 0, 255, 255};
constexpr scenic::Color kLowerLeftColor = {255, 0, 0, 255};
constexpr scenic::Color kLowerRightColor = {255, 0, 255, 255};
constexpr scenic::Color kCenterColor = {0, 255, 0, 255};

// This test verifies that the scene owner correctly connects the scene graph to
// the display so that pixels render, and enforces the expected presentation
// semantics.
class PresentationTest
    : public gtest::RealLoopFixture,
      public ::testing::WithParamInterface<ui_testing::UITestRealm::SceneOwnerType> {
 protected:
  // |testing::Test|
  void SetUp() override {
    ui_testing::UITestRealm::Config config;
    config.scene_owner = GetParam();
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

    scenic_ = realm_exposed_services_->Connect<fuchsia::ui::scenic::Scenic>();

    // Attach view, and wait for it to render.
    ui_test_manager_->InitializeScene();
    RunLoopUntil([this]() { return ui_test_manager_->ClientViewIsRendering(); });
  }

  // TODO(fxbug.dev/107927): Use fuchsia.ui.composition.Screenshot.
  scenic::Screenshot TakeScreenshot() {
    fuchsia::ui::scenic::ScreenshotData screenshot_out;
    scenic_->TakeScreenshot(
        [this, &screenshot_out](fuchsia::ui::scenic::ScreenshotData screenshot, bool status) {
          EXPECT_TRUE(status) << "Failed to take screenshot";
          screenshot_out = std::move(screenshot);
          QuitLoop();
        });

    FX_LOGS(INFO) << "Waiting for screenshot";
    RunLoop();
    return scenic::Screenshot(screenshot_out);
  }

  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<Realm> realm_;

  std::unique_ptr<ui_testing::TestView> test_view_;

  fuchsia::ui::scenic::ScenicPtr scenic_;
};

INSTANTIATE_TEST_SUITE_P(PresentationTestWithParams, PresentationTest,
                         ::testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                           ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER));

TEST_P(PresentationTest, RenderCoordinateGridPattern) {
  scenic::Screenshot screenshot = TakeScreenshot();

  EXPECT_EQ(screenshot.ColorAt(.25f, .25f), kUpperLeftColor);
  EXPECT_EQ(screenshot.ColorAt(.25f, .75f), kUpperRightColor);
  EXPECT_EQ(screenshot.ColorAt(.75f, .25f), kLowerLeftColor);
  EXPECT_EQ(screenshot.ColorAt(.75f, .75f), kLowerRightColor);
  EXPECT_EQ(screenshot.ColorAt(.5f, .5f), kCenterColor);
}

}  // namespace integration_tests
