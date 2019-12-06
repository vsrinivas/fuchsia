// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnifier.h"
#include "src/ui/scenic/lib/gfx/tests/pixel_test.h"
#include "src/ui/testing/views/coordinate_test_view.h"

namespace {

constexpr char kEnvironment[] = "MagnificationPixelTest";

// These tests leverage the coordinate test view to ensure that RootPresenter magnification APIs are
// working properly. From coordinate_test_view.h:
// ___________________________________
// |                |                |
// |     BLACK      |        RED     |
// |           _____|_____           |
// |___________|  GREEN  |___________|
// |           |_________|           |
// |                |                |
// |      BLUE      |     MAGENTA    |
// |________________|________________|
//
// These are rough integration tests to supplement the |ScenicPixelTest| clip-space transform tests.
class MagnificationPixelTest : public gfx::PixelTest {
 protected:
  MagnificationPixelTest() : gfx::PixelTest(kEnvironment) {}

  // |testing::Test|
  void SetUp() override {
    PixelTest::SetUp();
    view_ = std::make_unique<scenic::CoordinateTestView>(CreatePresentationContext());
    RunUntilIndirectPresent(view_.get());
  }

  // |gfx::PixelTest|
  std::unique_ptr<sys::testing::EnvironmentServices> CreateServices() override {
    auto services = PixelTest::CreateServices();
    // Publish the |fuchsia.accessibility.Magnifier| (mock impl) for RootPresenter to register its
    // presentations with.
    services->AddService(magnifier_bindings_.GetHandler(&magnifier_));
    return services;
  }

  // Blocking wrapper around |fuchsia.accessibility.MagnificationHandler.SetClipSpaceTransform| on
  // the presentation registered with the mock magnifier.
  void SetClipSpaceTransform(float x, float y, float scale) {
    ASSERT_TRUE(magnifier_.handler());

    magnifier_.handler().set_error_handler([](zx_status_t status) {
      FAIL() << "fuchsia.accessibility.MagnificationHandler closed: "
             << zx_status_get_string(status);
    });
    magnifier_.handler()->SetClipSpaceTransform(x, y, scale, [this] { QuitLoop(); });
    RunLoop();
  }

 private:
  accessibility_test::MockMagnifier magnifier_;
  fidl::BindingSet<fuchsia::accessibility::Magnifier> magnifier_bindings_;
  std::unique_ptr<scenic::CoordinateTestView> view_;
};

TEST_F(MagnificationPixelTest, Identity) {
  SetClipSpaceTransform(0, 0, 1);
  scenic::Screenshot screenshot = TakeScreenshot();

  EXPECT_EQ(scenic::CoordinateTestView::kUpperLeft, screenshot.ColorAt(.25f, .25f));
  EXPECT_EQ(scenic::CoordinateTestView::kUpperRight, screenshot.ColorAt(.25f, .75f));
  EXPECT_EQ(scenic::CoordinateTestView::kLowerLeft, screenshot.ColorAt(.75f, .25f));
  EXPECT_EQ(scenic::CoordinateTestView::kLowerRight, screenshot.ColorAt(.75f, .75f));
  EXPECT_EQ(scenic::CoordinateTestView::kCenter, screenshot.ColorAt(.5f, .5f));
}

TEST_F(MagnificationPixelTest, Center) {
  SetClipSpaceTransform(0, 0, 4);
  scenic::Screenshot screenshot = TakeScreenshot();

  EXPECT_EQ(scenic::CoordinateTestView::kCenter, screenshot.ColorAt(.25f, .25f));
  EXPECT_EQ(scenic::CoordinateTestView::kCenter, screenshot.ColorAt(.25f, .75f));
  EXPECT_EQ(scenic::CoordinateTestView::kCenter, screenshot.ColorAt(.75f, .25f));
  EXPECT_EQ(scenic::CoordinateTestView::kCenter, screenshot.ColorAt(.75f, .75f));
}

TEST_F(MagnificationPixelTest, UpperLeft) {
  SetClipSpaceTransform(1, 1, 2);
  scenic::Screenshot screenshot = TakeScreenshot();

  EXPECT_EQ(scenic::CoordinateTestView::kUpperLeft, screenshot.ColorAt(.25f, .25f));
  EXPECT_EQ(scenic::CoordinateTestView::kUpperLeft, screenshot.ColorAt(.25f, .75f));
  EXPECT_EQ(scenic::CoordinateTestView::kUpperLeft, screenshot.ColorAt(.75f, .25f));
  EXPECT_EQ(scenic::CoordinateTestView::kCenter, screenshot.ColorAt(.75f, .75f));
}

// WTB: test case under screen rotation

}  // namespace
