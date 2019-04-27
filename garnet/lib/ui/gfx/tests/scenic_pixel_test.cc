// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include "garnet/lib/ui/util/glm_workaround.h"
// clang-format on

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <gtest/gtest.h>
#include <lib/async/cpp/task.h>
#include <lib/escher/hmd/pose_buffer.h>
#include <lib/fsl/vmo/vector.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <src/lib/fxl/logging.h>
#include <zircon/status.h>

#include <map>
#include <string>
#include <vector>

#include "garnet/testing/views/background_view.h"
#include "garnet/testing/views/coordinate_test_view.h"
#include "garnet/testing/views/test_view.h"
#include "garnet/public/lib/escher/impl/vulkan_utils.h"
#include "garnet/lib/ui/gfx/tests/vk_session_test.h"
#include "public/lib/escher/test/gtest_vulkan.h"

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

    bool present_received = false;
    view->set_present_callback(
        [&present_received](fuchsia::images::PresentationInfo info) {
          present_received = true;
        });

    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&present_received] { return present_received; }, zx::sec(10)));
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

// Draws a white rectangle on a black background rendered with a stereo
// camera, which produces an image something like this:
// _____________________________________
// |                                   |
// |   ___________       ___________   |
// |   |         |       |         |   |
// |   |         |       |         |   |
// |   |  WHITE  | BLACK |  WHITE  |   |
// |   |         |       |         |   |
// |   |_________|       |_________|   |
// |                                   |
// |___________________________________|
//

TEST_F(ScenicPixelTest, StereoCamera) {
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

  static const float viewport_width = display_width / 2;
  static const float viewport_height = display_height;

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
  scenic::StereoCamera camera(scene);

  float camera_offset = 1001;
  float eye_position[3] = {display_width / 2.f, display_height / 2.f,
                           -camera_offset};
  float look_at[3] = {display_width / 2.f, display_height / 2.f, 1};
  float up[3] = {0, -1, 0};
  camera.SetTransform(eye_position, look_at, up);

  float fovy = 2 * atan((display_height / 2.f) / abs(eye_position[2]));
  glm::mat4 projection = glm::perspective(
      fovy, viewport_width / viewport_height, 0.1f, camera_offset);
  projection = glm::scale(projection, glm::vec3(1.f, -1.f, 1.f));

  camera.SetStereoProjection(glm::value_ptr(projection),
                             glm::value_ptr(projection));

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

  static const float pane_width = viewport_width / 2;
  static const float pane_height = viewport_height / 2;

  glm::vec3 translation(display_width * 0.5, display_height * 0.5, -10);

  scenic::Rectangle pane_shape(session, pane_width, pane_height);

  scenic::Material pane_material(session);
  pane_material.SetColor(255, 255, 255, 255);

  scenic::ShapeNode pane_shape_node(session);
  pane_shape_node.SetShape(pane_shape);
  pane_shape_node.SetMaterial(pane_material);
  pane_shape_node.SetTranslation(translation.x, translation.y, translation.z);
  root_node.AddChild(pane_shape_node);

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

  // Color array to index 0=BLACK 1=WHITE
  scenic::Color colors[2] = {scenic::Color({0, 0, 0, 0}),
                             scenic::Color({255, 255, 255, 255})};

  // Expected results by index into colors array. Column major.
  // Note how this is a transposed, low-res version of the scene being drawn.
  // clang-format off
  int expected[8][4] = {{0, 0, 0, 0},
                        {0, 1, 1, 0},
                        {0, 1, 1, 0},
                        {0, 0, 0, 0},
                        {0, 0, 0, 0},
                        {0, 1, 1, 0},
                        {0, 1, 1, 0},
                        {0, 0, 0, 0}};
  // clang-format on

  // Test 8 columns of 4 samples each
  int num_x_samples = 8;
  int num_y_samples = 4;
  float x_step = 1.f / num_x_samples;
  float y_step = 1.f / num_y_samples;
  // i maps to x, j maps to y
  for (int i = 0; i < num_x_samples; i++) {
    for (int j = 0; j < num_y_samples; j++) {
      float x = x_step / 2 + i * x_step;
      float y = y_step / 2 + j * y_step;
      EXPECT_EQ(colors[expected[i][j]], get_color_at_coordinates(x, y))
          << "i = " << i << ", j = " << j << ", Sample Location: {" << x << ", "
          << y << "}";
    }
  }
}

// At a high level this test puts a camera inside a cube where each face is a
// different color, then uses a pose buffer to point the camera at different
// faces, using the colors to verify the pose buffer is working as expected.

VK_TEST_F(ScenicPixelTest, PoseBuffer) {
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
  scenic::StereoCamera camera(scene);

  static const float viewport_width = display_width / 2;
  static const float viewport_height = display_height;
  static const float camera_offset = 500;
  // View matrix matches vulkan clip space +Y down, looking in direction of +Z
  static const glm::vec3 eye(display_width / 2.f, display_height / 2.f,
                             -camera_offset);
  static const glm::vec3 look_at(eye + glm::vec3(0, 0, 1));
  static const glm::vec3 up(0, -1, 0);

  camera.SetTransform(glm::value_ptr(eye), glm::value_ptr(look_at),
                      glm::value_ptr(up));

  glm::mat4 projection =
      glm::perspective(glm::radians(120.f), viewport_width / viewport_height,
                       0.1f, camera_offset);
  // projection = glm::scale(projection, glm::vec3(1.f, -1.f, 1.f));

  glm::mat4 clip(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f);
  projection = clip * projection;

  glm::mat4 view = glm::lookAt(eye, look_at, up);

  camera.SetStereoProjection(glm::value_ptr(projection),
                             glm::value_ptr(projection));

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

  // Configure PoseBuffer

  const size_t kVmoSize = PAGE_SIZE;
  zx_status_t status;

  auto vulkan_queues =
      scenic_impl::gfx::test::VkSessionTest::CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  // TODO(SCN-1369): Scenic may use a different set of bits when creating a
  // buffer, resulting in a memory pool mismatch.
  const vk::BufferUsageFlags kUsageFlags =
      vk::BufferUsageFlagBits::eTransferSrc |
      vk::BufferUsageFlagBits::eTransferDst |
      vk::BufferUsageFlagBits::eStorageTexelBuffer |
      vk::BufferUsageFlagBits::eStorageBuffer |
      vk::BufferUsageFlagBits::eIndexBuffer |
      vk::BufferUsageFlagBits::eVertexBuffer;

  auto memory_requirements =
      scenic_impl::gfx::test::VkSessionTest::GetBufferRequirements(
          device, kVmoSize, kUsageFlags);
  auto memory = scenic_impl::gfx::test::VkSessionTest::AllocateExportableMemory(
      device, physical_device, memory_requirements,
      vk::MemoryPropertyFlagBits::eDeviceLocal |
          vk::MemoryPropertyFlagBits::eHostVisible);

  // If we can't make memory that is both host-visible and device-local, we
  // can't run this test.
  if (!memory) {
    FXL_LOG(INFO)
        << "Could not find UMA compatible memory pool, aborting test.";
    return;
  }

  zx::vmo pose_buffer_vmo =
      scenic_impl::gfx::test::VkSessionTest::ExportMemoryAsVmo(
          device, vulkan_queues->dispatch_loader(), memory);

  zx::vmo remote_vmo;
  status = pose_buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &remote_vmo);
  FXL_DCHECK(status == ZX_OK);

  zx_time_t base_time = zx::clock::get_monotonic().get();
  // Normally the time interval is the period of time between each entry in the
  // pose buffer. In this example we only use one entry so the time interval is
  // pretty meaningless. Set to 1 for simplicity (see ARGO-21).
  zx_time_t time_interval = 1;
  uint32_t num_entries = 1;

  scenic::Memory mem(session, std::move(remote_vmo), kVmoSize,
                     fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  scenic::Buffer pose_buffer(mem, 0, kVmoSize);

  camera.SetPoseBuffer(pose_buffer, num_entries, base_time, time_interval);

  // Setup Scene.

  float pane_width = camera_offset / 2.f;
  scenic::Rectangle pane_shape(session, pane_width, pane_width);

  static const int num_panes = 6;

  scenic::Color colors[num_panes] = {
      scenic::Color({255, 0, 0, 255}),    // RED
      scenic::Color({0, 255, 255, 255}),  // CYAN
      scenic::Color({0, 255, 0, 255}),    // GREEN
      scenic::Color({255, 0, 255, 255}),  // MAGENTA
      scenic::Color({0, 0, 255, 255}),    // BLUE
      scenic::Color({255, 255, 0, 255}),  // YELLOW
  };

  static const float pane_offset = pane_width / 2;

  glm::vec3 translations[num_panes] = {
      eye + glm::vec3(0, 0, pane_offset),   // In front of camera.
      eye + glm::vec3(0, 0, -pane_offset),  // Behind camera.
      eye + glm::vec3(-pane_offset, 0, 0),  // Left of Camera
      eye + glm::vec3(pane_offset, 0, 0),   // Right of camera
      eye + glm::vec3(0, -pane_offset, 0),  // Above Camera
      eye + glm::vec3(0, pane_offset, 0),   // Below Camera
  };

  static const float pi = glm::pi<float>();
  glm::quat orientations[num_panes] = {
      glm::quat(),  // identity quaternion
      glm::angleAxis(pi, glm::vec3(1, 0, 0)),
      glm::angleAxis(-pi / 2, glm::vec3(0, 1, 0)),
      glm::angleAxis(pi / 2, glm::vec3(0, 1, 0)),
      glm::angleAxis(pi / 2, glm::vec3(1, 0, 0)),
      glm::angleAxis(-pi / 2, glm::vec3(1, 0, 0)),
  };

  for (int i = 0; i < num_panes; i++) {
    scenic::Color color = colors[i];
    glm::vec3 translation = translations[i];
    glm::quat orientation = orientations[i];

    FXL_LOG(ERROR) << "translation: " << glm::to_string(translation);
    FXL_LOG(ERROR) << "orientation: " << glm::to_string(orientation);

    scenic::Material pane_material(session);
    pane_material.SetColor(color.r, color.g, color.b, color.a);
    scenic::ShapeNode pane_shape_node(session);
    pane_shape_node.SetShape(pane_shape);
    pane_shape_node.SetMaterial(pane_material);
    pane_shape_node.SetTranslation(translation.x, translation.y, translation.z);
    pane_shape_node.SetRotation(orientation.x, orientation.y, orientation.z,
                                orientation.w);
    root_node.AddChild(pane_shape_node);
  }

  static const int num_quaternions = 8;

  glm::quat quaternions[num_quaternions] = {
      glm::quat(),                                 // dead ahead
      glm::angleAxis(pi, glm::vec3(0, 0, 1)),      // dead ahead but upside down
      glm::angleAxis(pi, glm::vec3(1, 0, 0)),      // behind around X
      glm::angleAxis(pi, glm::vec3(0, 1, 0)),      // behind around Y
      glm::angleAxis(pi / 2, glm::vec3(0, 1, 0)),  // left
      glm::angleAxis(-pi / 2, glm::vec3(0, 1, 0)),  // right
      glm::angleAxis(pi / 2, glm::vec3(1, 0, 0)),   // up
      glm::angleAxis(-pi / 2, glm::vec3(1, 0, 0)),  // down
  };

  int expected_color_index[num_quaternions] = {0, 0, 1, 1, 2, 3, 4, 5};

  for (int i = 0; i < num_quaternions; i++) {
    // Put pose into pose buffer.
    // Only testing orientation so position is always the origin.
    // Quaternion describes head orientation, so invert it to get a transform
    // that takes you into head space.
    escher::hmd::Pose pose(glm::inverse(quaternions[i]), glm::vec3(0, 0, 0));

    // Use vmo::write here for test simplicity. In a real case the vmo should be
    // mapped into a vmar so we dont need a syscall per write
    zx_status_t status =
        pose_buffer_vmo.write(&pose, 0, sizeof(escher::hmd::Pose));
    FXL_DCHECK(status == ZX_OK);

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

    EXPECT_EQ(colors[expected_color_index[i]],
              get_color_at_coordinates(0.25, 0.5))
        << "i = " << i;
  }
  device.freeMemory(memory);
}

}  // namespace
