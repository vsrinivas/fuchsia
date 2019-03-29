// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>
#include <vector>

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/async/cpp/task.h>
#include <lib/fsl/vmo/vector.h>
#include <src/lib/fxl/logging.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include "garnet/testing/views/background_view.h"
#include "garnet/testing/views/coordinate_test_view.h"
#include "garnet/testing/views/test_view.h"

namespace {

constexpr char kEnvironment[] = "ScenicPixelTest";
constexpr zx::duration kTimeout = zx::sec(15);

// These tests need Scenic and RootPresenter at minimum, which expand to the
// dependencies below. Using |TestWithEnvironment|, we use
// |fuchsia.sys.Environment| and |fuchsia.sys.Loader| from the system (declared
// in our *.cmx sandbox) and launch these other services in the environment we
// create in our test fixture.
//
// Another way to do this would be to whitelist these services in our sandbox
// and inject/start them via the |fuchsia.test| facet. However that has the
// disadvantage that it uses one instance of those services across all tests in
// the binary, making each test not hermetic wrt. the others. A trade-off is
// that the |TestWithEnvironment| method is more verbose.
const std::map<std::string, std::string> kServices = {
    {"fuchsia.tracelink.Registry",
     "fuchsia-pkg://fuchsia.com/trace_manager#meta/trace_manager.cmx"},
    {"fuchsia.ui.policy.Presenter",
     "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx"},
    {"fuchsia.ui.scenic.Scenic",
     "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx"},
    {"fuchsia.vulkan.loader.Loader",
     "fuchsia-pkg://fuchsia.com/vulkan_loader#meta/vulkan_loader.cmx"},
    {"fuchsia.sysmem.Allocator",
     "fuchsia-pkg://fuchsia.com/sysmem_connector#meta/sysmem_connector.cmx"}};

// Test fixture that sets up an environment suitable for Scenic pixel tests
// and provides related utilities. The environment includes Scenic and
// RootPresenter, and their dependencies.
class ScenicPixelTest : public sys::testing::TestWithEnvironment {
 protected:
  ScenicPixelTest() {
    std::unique_ptr<sys::testing::EnvironmentServices> services =
        CreateServices();

    for (const auto& entry : kServices) {
      fuchsia::sys::LaunchInfo launch_info;
      launch_info.url = entry.second;
      services->AddServiceWithLaunchInfo(std::move(launch_info), entry.first);
    }

    environment_ =
        CreateNewEnclosingEnvironment(kEnvironment, std::move(services));

    environment_->ConnectToService(scenic_.NewRequest());
    scenic_.set_error_handler([this](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });
  }

  // Blocking wrapper around |Scenic::TakeScreenshot|. This should not be called
  // from within a loop |Run|, as it spins up its own to block and nested loops
  // are undefined behavior.
  fuchsia::ui::scenic::ScreenshotData TakeScreenshot() {
    fuchsia::ui::scenic::ScreenshotData screenshot_out;
    scenic_->TakeScreenshot(
        [this, &screenshot_out](fuchsia::ui::scenic::ScreenshotData screenshot,
                                bool status) {
          EXPECT_TRUE(status) << "Failed to take screenshot";
          screenshot_out = std::move(screenshot);
          QuitLoop();
        });
    EXPECT_FALSE(RunLoopWithTimeout(kTimeout))
        << "Timed out waiting for screenshot.";
    return screenshot_out;
  }

  // Create a |ViewContext| that allows us to present a view via
  // |RootPresenter|. See also examples/ui/hello_base_view
  scenic::ViewContext CreatePresentationContext() {
    auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

    scenic::ViewContext view_context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get()),
        .view_token2 = std::move(view_token),
    };

    fuchsia::ui::policy::PresenterPtr presenter;
    environment_->ConnectToService(presenter.NewRequest());
    presenter->PresentView(std::move(view_holder_token), nullptr);

    return view_context;
  }

  // Runs until the view renders its next frame. Technically, waits until the
  // |Present| callback is invoked with an expected presentation timestamp, and
  // then waits until that time.
  void RunUntilPresent(scenic::TestView* view) {
    // Typical sequence of events:
    // 1. We set up a view bound as a |SessionListener|.
    // 2. The view sends its initial |Present| to get itself connected, without
    //    a callback.
    // 3. We call |RunUntilPresent| which sets a present callback on our
    //    |TestView|.
    // 4. |RunUntilPresent| runs the message loop, which allows the view to
    //    receive a Scenic event telling us our metrics.
    // 5. In response, the view sets up the scene graph with the test scene.
    // 6. The view calls |Present| with the callback set in |RunUntilPresent|.
    // 7. The still-running message loop eventually dispatches the present
    //    callback.
    // 8. The callback schedules a quit for the presentation timestamp we got.
    // 9. The message loop eventually dispatches the quit and exits.

    view->set_present_callback([this](fuchsia::images::PresentationInfo info) {
      zx::time presentation_time =
          static_cast<zx::time>(info.presentation_time);
      FXL_LOG(INFO)
          << "Present scheduled for "
          << (presentation_time - zx::clock::get_monotonic()).to_msecs()
          << " ms from now";
      async::PostTaskForTime(dispatcher(), QuitLoopClosure(),
                             presentation_time);
    });

    EXPECT_FALSE(RunLoopWithTimeout(kTimeout))
        << "Timed out waiting for present. See surrounding logs for details.";
  }

  fuchsia::ui::scenic::ScenicPtr scenic_;

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
};

TEST_F(ScenicPixelTest, SolidColor) {
  scenic::BackgroundView view(CreatePresentationContext());
  RunUntilPresent(&view);

  fuchsia::ui::scenic::ScreenshotData screenshot = TakeScreenshot();

  EXPECT_GT(screenshot.info.width, 0u);
  EXPECT_GT(screenshot.info.height, 0u);

  // We could assert on each pixel individually, but a histogram might give us a
  // more meaningful failure.
  std::map<scenic::Color, size_t> histogram = scenic::Histogram(screenshot);

  EXPECT_GT(histogram[scenic::BackgroundView::kBackgroundColor], 0u);
  histogram.erase(scenic::BackgroundView::kBackgroundColor);
  EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram)
      << "Unexpected colors";
}

TEST_F(ScenicPixelTest, ViewCoordinates) {
  // Synchronously get display dimensions
  float display_width;
  float display_height;
  scenic_->GetDisplayInfo([this, &display_width, &display_height](
                              fuchsia::ui::gfx::DisplayInfo display_info) {
    display_width = static_cast<float>(display_info.width_in_px);
    display_height = static_cast<float>(display_info.height_in_px);
    QuitLoop();
  });
  RunLoop();

  scenic::CoordinateTestView view(CreatePresentationContext());
  RunUntilPresent(&view);

  fuchsia::ui::scenic::ScreenshotData screenshot = TakeScreenshot();
  std::vector<uint8_t> data;
  EXPECT_TRUE(fsl::VectorFromVmo(screenshot.data, &data))
      << "Failed to read screenshot";

  auto get_color_at_coordinates = [&display_width, &display_height, &data](
                                      float x, float y) -> scenic::Color {
    auto pixels = reinterpret_cast<scenic::Color*>(data.data());
    uint32_t index_x = x * display_width;
    uint32_t index_y = y * display_height;
    uint32_t index = index_y * display_width + index_x;
    return pixels[index];
  };

  EXPECT_EQ(scenic::Color({0, 0, 0, 255}),
            get_color_at_coordinates(.25f, .25f));
  EXPECT_EQ(scenic::Color({0, 0, 255, 255}),
            get_color_at_coordinates(.25f, .75f));
  EXPECT_EQ(scenic::Color({255, 0, 0, 255}),
            get_color_at_coordinates(.75f, .25f));
  EXPECT_EQ(scenic::Color({255, 0, 255, 255}),
            get_color_at_coordinates(.75f, .75f));
  EXPECT_EQ(scenic::Color({0, 255, 0, 255}),
            get_color_at_coordinates(.5f, .5f));
}

// Draws and tests the following coordinate test pattern without views:
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
TEST_F(ScenicPixelTest, GlobalCoordinates) {
  // Synchronously get display dimensions
  float display_width;
  float display_height;
  scenic_->GetDisplayInfo([this, &display_width, &display_height](
                              fuchsia::ui::gfx::DisplayInfo display_info) {
    display_width = static_cast<float>(display_info.width_in_px);
    display_height = static_cast<float>(display_info.height_in_px);
    QuitLoop();
  });
  RunLoop();

  // Initialize session
  auto unique_session = std::make_unique<scenic::Session>(scenic_.get());
  auto session = unique_session.get();
  session->set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Session terminated.";
    QuitLoop();
  });

  scenic::DisplayCompositor compositor(session);
  scenic::LayerStack layer_stack(session);
  scenic::Layer layer(session);
  scenic::Renderer renderer(session);
  scenic::Scene scene(session);
  scenic::Camera camera(scene);

  float eye_position[3] = {display_width / 2.f, display_height / 2.f, -1001};
  float look_at[3] = {display_width / 2.f, display_height / 2.f, 1};
  float up[3] = {0, -1, 0};
  camera.SetTransform(eye_position, look_at, up);

  compositor.SetLayerStack(layer_stack);
  layer_stack.AddLayer(layer);
  layer.SetSize(display_width, display_height);
  layer.SetRenderer(renderer);
  renderer.SetCamera(camera.id());

  // Set up lights.
  scenic::AmbientLight ambient_light(session);
  scene.AddLight(ambient_light);
  ambient_light.SetColor(1.f, 1.f, 1.f);

  // Create an EntityNode to serve as the scene root.
  scenic::EntityNode root_node(session);
  scene.AddChild(root_node.id());

  static const float pane_width = display_width / 2;
  static const float pane_height = display_height / 2;

  for (uint32_t i = 0; i < 2; i++) {
    for (uint32_t j = 0; j < 2; j++) {
      scenic::Rectangle pane_shape(session, pane_width, pane_height);
      scenic::Material pane_material(session);
      pane_material.SetColor(i * 255.f, 0, j * 255.f, 255);

      scenic::ShapeNode pane_node(session);
      pane_node.SetShape(pane_shape);
      pane_node.SetMaterial(pane_material);
      pane_node.SetTranslation((i + 0.5) * pane_width, (j + 0.5) * pane_height,
                               -20);
      root_node.AddChild(pane_node);
    }
  }

  scenic::Rectangle pane_shape(session, display_width / 4, display_height / 4);
  scenic::Material pane_material(session);
  pane_material.SetColor(0, 255, 0, 255);

  scenic::ShapeNode pane_node(session);
  pane_node.SetShape(pane_shape);
  pane_node.SetMaterial(pane_material);
  pane_node.SetTranslation(0.5 * display_width, 0.5 * display_height, -40);
  root_node.AddChild(pane_node);

  // Actual tests. Test the same scene with an orthographic and perspective
  // camera.
  std::string camera_type[2] = {"orthographic", "perspective"};
  float fov[2] = {0, 2 * atan((display_height / 2.f) / abs(eye_position[2]))};

  for (int i = 0; i < 2; i++) {
    FXL_LOG(INFO) << "Testing " << camera_type[i] << " camera";
    camera.SetProjection(fov[i]);

    session->Present(
        0, [this](fuchsia::images::PresentationInfo info) { QuitLoop(); });
    RunLoop();

    fuchsia::ui::scenic::ScreenshotData screenshot = TakeScreenshot();
    std::vector<uint8_t> data;
    EXPECT_TRUE(fsl::VectorFromVmo(screenshot.data, &data))
        << "Failed to read screenshot";

    auto get_color_at_coordinates = [&display_width, &display_height, &data](
                                        float x, float y) -> scenic::Color {
      auto pixels = reinterpret_cast<scenic::Color*>(data.data());
      uint32_t index_x = x * display_width;
      uint32_t index_y = y * display_height;
      uint32_t index = index_y * display_width + index_x;
      return pixels[index];
    };

    EXPECT_EQ(scenic::Color({0, 0, 0, 255}),
              get_color_at_coordinates(.25f, .25f));
    EXPECT_EQ(scenic::Color({0, 0, 255, 255}),
              get_color_at_coordinates(.25f, .75f));
    EXPECT_EQ(scenic::Color({255, 0, 0, 255}),
              get_color_at_coordinates(.75f, .25f));
    EXPECT_EQ(scenic::Color({255, 0, 255, 255}),
              get_color_at_coordinates(.75f, .75f));
    EXPECT_EQ(scenic::Color({0, 255, 0, 255}),
              get_color_at_coordinates(.5f, .5f));
  }
}

}  // namespace
