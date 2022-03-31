// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/ui/base_view/base_view.h"
#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnifier.h"
#include "src/ui/scenic/tests/gfx_integration_tests/pixel_test.h"
#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"
#include "src/ui/testing/views/coordinate_test_view.h"

namespace integration_tests {

// HACK(fxbug.dev/42459): This allows the test to feed in a clip-space transform that is
// semantically invariant against screen rotation. The only non-identity rotation we expect to run
// against soon is 270 degrees. This doesn't generalize well, so it should be temporary.
bool IsScreenRotated() {
  // This also lives in root_presenter/app.cc
  std::string rotation_value;

  return files::ReadFileToString("/config/data/display_rotation", &rotation_value) &&
         atoi(rotation_value.c_str()) == 270;
}

class MockMagnifierImpl : public accessibility_test::MockMagnifier,
                          public component_testing::LocalComponent {
 public:
  explicit MockMagnifierImpl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // |fuchsia::accessibility::Magnifier|
  void RegisterHandler(
      fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) override {
    handler_ = handler.Bind();
    handler_.set_error_handler([](zx_status_t status) {
      FAIL() << "fuchsia.accessibility.MagnificationHandler closed: "
             << zx_status_get_string(status);
    });
  }

  // |MockComponent::Start|
  // When the component framework requests for this component to start, this
  // method will be invoked by the realm_builder library.
  void Start(std::unique_ptr<component_testing::LocalComponentHandles> mock_handles) override {
    // When this component starts, add a binding to the fuchsia.accessibility.Magnifier
    // protocol to this component's outgoing directory.
    FX_CHECK(
        mock_handles->outgoing()->AddPublicService(
            fidl::InterfaceRequestHandler<fuchsia::accessibility::Magnifier>([this](auto request) {
              bindings_.AddBinding(this, std::move(request), dispatcher_);
            })) == ZX_OK);
    mock_handles_ = std::move(mock_handles);
  }

  bool IsBound() { return handler_.is_bound(); }

  fuchsia::accessibility::MagnificationHandler* handler() { return handler_.get(); }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::unique_ptr<component_testing::LocalComponentHandles> mock_handles_;
  fidl::BindingSet<fuchsia::accessibility::Magnifier> bindings_;
  fuchsia::accessibility::MagnificationHandlerPtr handler_;
};

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
class MagnificationPixelTest : public PixelTest {
 protected:
  // |testing::Test|
  void SetUp() override {
    magnifier_ = std::make_unique<MockMagnifierImpl>(dispatcher());

    PixelTest::SetUp();

    view_ = std::make_unique<scenic::CoordinateTestView>(CreatePresentationContext());
    RunUntilIndirectPresent(view_.get());
  }

  // Blocking wrapper around |fuchsia.accessibility.MagnificationHandler.SetClipSpaceTransform| on
  // the presentation registered with the mock magnifier.
  void SetClipSpaceTransform(float x, float y, float scale) {
    ASSERT_TRUE(magnifier_->handler());

    magnifier_->handler()->SetClipSpaceTransform(x, y, scale, [this] { QuitLoop(); });
    RunLoop();
  }

 private:
  RealmRoot SetupRealm() override {
    RealmBuilderArgs args = {.scene_owner = SceneOwner::ROOT_PRESENTER_LEGACY};
    const std::string mock_component_name = "mock_magnifier";

    return ScenicRealmBuilder(std::move(args))
        .AddRealmProtocol(fuchsia::ui::scenic::Scenic::Name_)
        .AddRealmProtocol(fuchsia::ui::annotation::Registry::Name_)
        .AddSceneOwnerProtocol(fuchsia::ui::policy::Presenter::Name_)
        .AddMockComponent({.name = mock_component_name, .impl = magnifier_.get()})
        .RouteMockComponentProtocolToSceneOwner(mock_component_name,
                                                fuchsia::accessibility::Magnifier::Name_)
        .Build();
  }

  std::unique_ptr<MockMagnifierImpl> magnifier_;
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
  if (!IsScreenRotated()) {
    SetClipSpaceTransform(1, 1, 2);
  } else {
    // On 270-rotated devices, the user-oriented upper left is the display's lower left.
    //
    // (0,h)___________________________________(0,0)
    //      |                |                |
    //      |     BLACK      |        RED     |
    //      |           _____|_____           |
    //      |___________|  GREEN  |___________|
    //      |           |_________|           |
    //      |                |                |
    //      |      BLUE      |     MAGENTA    |
    //      |________________|________________|
    // (w,h)                                   (w,0)
    //
    // The screenshot has rotation applied so that it matches user orientation.
    SetClipSpaceTransform(1, -1, 2);
  }

  scenic::Screenshot screenshot = TakeScreenshot();

  EXPECT_EQ(scenic::CoordinateTestView::kUpperLeft, screenshot.ColorAt(.25f, .25f));
  EXPECT_EQ(scenic::CoordinateTestView::kUpperLeft, screenshot.ColorAt(.25f, .75f));
  EXPECT_EQ(scenic::CoordinateTestView::kUpperLeft, screenshot.ColorAt(.75f, .25f));
  EXPECT_EQ(scenic::CoordinateTestView::kCenter, screenshot.ColorAt(.75f, .75f));
}

// WTB: test case under screen rotation

}  // namespace integration_tests
