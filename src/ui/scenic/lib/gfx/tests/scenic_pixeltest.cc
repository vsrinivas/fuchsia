// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <fuchsia/images/cpp/fidl.h>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <gtest/gtest.h>
#include <lib/fdio/directory.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/images/cpp/images.h>
#include <lib/zx/clock.h>
#include <zircon/types.h>

#include <map>
#include <string>

#include "src/lib/fsl/handles/object_info.h"
#include <lib/syslog/cpp/macros.h>
#include "src/ui/scenic/lib/gfx/tests/pixel_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_util.h"
#include "src/ui/testing/views/background_view.h"
#include "src/ui/testing/views/coordinate_test_view.h"
#include "src/ui/testing/views/opacity_view.h"
#include "src/ui/testing/views/test_view.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/hmd/pose_buffer.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/yuv/yuv.h"

#include <vulkan/vulkan.hpp>

namespace {

constexpr char kEnvironment[] = "ScenicPixelTest";
// If you change the size of YUV buffers, make sure that the YUV test in
// host_image_unittest.cc is also updated. Unlike that unit test,
// scenic_pixel_test.cc has no way to confirm that it is going through the
// direct-to-GPU path.
// TODO(SCN-1387): This number needs to be queried via sysmem or vulkan.
constexpr uint32_t kYuvSize = 64;

const float kPi = glm::pi<float>();

class ScenicPixelTest : public gfx::PixelTest {
 protected:
  ScenicPixelTest() : gfx::PixelTest(kEnvironment) {}
};

TEST_F(ScenicPixelTest, SolidColor) {
  scenic::BackgroundView view(CreatePresentationContext());
  view.SetBackgroundColor(scenic::BackgroundView::kBackgroundColor);
  RunUntilIndirectPresent(&view);

  scenic::Screenshot screenshot = TakeScreenshot();
  ASSERT_FALSE(screenshot.empty());

  // We could assert on each pixel individually, but a histogram might give us a
  // more meaningful failure.
  std::map<scenic::Color, size_t> histogram = screenshot.Histogram();

  EXPECT_GT(histogram[scenic::BackgroundView::kBackgroundColor], 0u);
  histogram.erase(scenic::BackgroundView::kBackgroundColor);
  // This assert is written this way so that, when it fails, it prints out all
  // the unexpected colors
  EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
}

TEST_F(ScenicPixelTest, PresentOrReplaceView_ShouldReplacePreviousPresentation) {
  scenic::BackgroundView view(CreatePresentationContext());
  view.SetBackgroundColor(scenic::BackgroundView::kBackgroundColor);
  RunUntilIndirectPresent(&view);

  {
    scenic::Screenshot screenshot = TakeScreenshot();
    ASSERT_FALSE(screenshot.empty());

    std::map<scenic::Color, size_t> histogram = screenshot.Histogram();
    EXPECT_GT(histogram[scenic::BackgroundView::kBackgroundColor], 0u);
    histogram.erase(scenic::BackgroundView::kBackgroundColor);
    EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
  }

  const scenic::Color kNewBackgroundColor = {0xFF, 0x00, 0xFF, 0xFF};
  ASSERT_FALSE(kNewBackgroundColor == scenic::BackgroundView::kBackgroundColor);

  {
    // Clobber current presentation with a new one with different background. Check that the
    // background changes.
    scenic::BackgroundView view2(CreatePresentationContext(/*clobber=*/true));
    view2.SetBackgroundColor(kNewBackgroundColor);
    RunUntilIndirectPresent(&view2);

    scenic::Screenshot screenshot = TakeScreenshot();
    ASSERT_FALSE(screenshot.empty());

    std::map<scenic::Color, size_t> histogram = screenshot.Histogram();
    EXPECT_GT(histogram[kNewBackgroundColor], 0u);
    histogram.erase(kNewBackgroundColor);
    EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
  }
}

TEST_F(ScenicPixelTest, NV12Texture) {
  scenic::BackgroundView view(CreatePresentationContext());
  fuchsia::images::ImageInfo image_info{
      .width = kYuvSize,
      .height = kYuvSize,
      .stride = static_cast<uint32_t>(
          kYuvSize * images::StrideBytesPerWidthPixel(fuchsia::images::PixelFormat::NV12)),
      .pixel_format = fuchsia::images::PixelFormat::NV12,
  };

  uint32_t num_pixels = image_info.width * image_info.height;
  uint64_t image_vmo_bytes = images::ImageSize(image_info);
  EXPECT_EQ((3 * num_pixels) / 2, image_vmo_bytes);

  zx::vmo image_vmo;
  zx_status_t status = zx::vmo::create(image_vmo_bytes, 0, &image_vmo);
  EXPECT_EQ(ZX_OK, status);
  uint8_t* vmo_base;
  status = zx::vmar::root_self()->map(0, image_vmo, 0, image_vmo_bytes,
                                      ZX_VM_PERM_WRITE | ZX_VM_PERM_READ,
                                      reinterpret_cast<uintptr_t*>(&vmo_base));
  EXPECT_EQ(ZX_OK, status);

  static const uint8_t kYValue = 110;
  static const uint8_t kUValue = 192;
  static const uint8_t kVValue = 192;
  const scenic::Color kBgraColor = {0xF1, 0x87, 0xFA, 0xFF};

  // Set all the Y pixels at full res.
  for (uint32_t i = 0; i < num_pixels; ++i) {
    vmo_base[i] = kYValue;
  }

  // Set all the UV pixels pairwise at half res.
  for (uint32_t i = num_pixels; i < num_pixels + num_pixels / 2; i += 2) {
    vmo_base[i] = kUValue;
    vmo_base[i + 1] = kVValue;
  }

  view.SetImage(std::move(image_vmo), image_vmo_bytes, image_info,
                fuchsia::images::MemoryType::HOST_MEMORY);
  RunUntilIndirectPresent(&view);

  scenic::Screenshot screenshot = TakeScreenshot();
  ASSERT_FALSE(screenshot.empty());

  // We could assert on each pixel individually, but a histogram might give us a
  // more meaningful failure.
  std::map<scenic::Color, size_t> histogram = screenshot.Histogram();

  EXPECT_GT(histogram[kBgraColor], 0u);
  histogram.erase(kBgraColor);

  // This assert is written this way so that, when it fails, it prints out all
  // the unexpected colors
  EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
}

TEST_F(ScenicPixelTest, ViewCoordinates) {
  scenic::CoordinateTestView view(CreatePresentationContext());
  RunUntilIndirectPresent(&view);

  scenic::Screenshot screenshot = TakeScreenshot();

  EXPECT_EQ(scenic::CoordinateTestView::kUpperLeft, screenshot.ColorAt(.25f, .25f));
  EXPECT_EQ(scenic::CoordinateTestView::kUpperRight, screenshot.ColorAt(.25f, .75f));
  EXPECT_EQ(scenic::CoordinateTestView::kLowerLeft, screenshot.ColorAt(.75f, .25f));
  EXPECT_EQ(scenic::CoordinateTestView::kLowerRight, screenshot.ColorAt(.75f, .75f));
  EXPECT_EQ(scenic::CoordinateTestView::kCenter, screenshot.ColorAt(.5f, .5f));
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
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  scenic::Scene* const scene = &test_session->scene;

  const float pane_width = display_width / 2;
  const float pane_height = display_height / 2;

  for (uint32_t i = 0; i < 2; i++) {
    for (uint32_t j = 0; j < 2; j++) {
      scenic::Rectangle pane_shape(session, pane_width, pane_height);
      scenic::Material pane_material(session);
      pane_material.SetColor(i * 255.f, 0, j * 255.f, 255);

      scenic::ShapeNode pane_node(session);
      pane_node.SetShape(pane_shape);
      pane_node.SetMaterial(pane_material);
      pane_node.SetTranslation((i + .5f) * pane_width, (j + .5f) * pane_height, -20);
      scene->AddChild(pane_node);
    }
  }

  scenic::Rectangle pane_shape(session, display_width / 4, display_height / 4);
  scenic::Material pane_material(session);
  pane_material.SetColor(0, 255, 0, 255);

  scenic::ShapeNode pane_node(session);
  pane_node.SetShape(pane_shape);
  pane_node.SetMaterial(pane_material);
  pane_node.SetTranslation(.5f * display_width, .5f * display_height, -40);
  scene->AddChild(pane_node);

  // Actual tests. Test the same scene with an orthographic and perspective
  // camera.
  std::string camera_type[2] = {"orthographic", "perspective"};
  auto camera = test_session->SetUpCamera();
  float fov[2] = {0, 2 * atan((display_height / 2.f) / gfx::TestSession::kDefaultCameraOffset)};

  for (int i = 0; i < 2; i++) {
    FX_LOGS(INFO) << "Testing " << camera_type[i] << " camera";
    camera.SetProjection(fov[i]);

    Present(session);
    scenic::Screenshot screenshot = TakeScreenshot();

    EXPECT_EQ(scenic::Color({0, 0, 0, 255}), screenshot.ColorAt(.25f, .25f));
    EXPECT_EQ(scenic::Color({0, 0, 255, 255}), screenshot.ColorAt(.25f, .75f));
    EXPECT_EQ(scenic::Color({255, 0, 0, 255}), screenshot.ColorAt(.75f, .25f));
    EXPECT_EQ(scenic::Color({255, 0, 255, 255}), screenshot.ColorAt(.75f, .75f));
    EXPECT_EQ(scenic::Color({0, 255, 0, 255}), screenshot.ColorAt(.5f, .5f));
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
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;

  const float viewport_width = display_width / 2;
  const float viewport_height = display_height;

  float fovy = 2 * atan((display_height / 2.f) / gfx::TestSession::kDefaultCameraOffset);
  glm::mat4 projection = glm::perspective(fovy, viewport_width / viewport_height, 0.1f,
                                          gfx::TestSession::kDefaultCameraOffset);
  projection = glm::scale(projection, glm::vec3(1.f, -1.f, 1.f));

  std::array<float, 16> projection_arr;
  auto projection_ptr = glm::value_ptr(projection);
  std::copy(projection_ptr, projection_ptr + 16, std::begin(projection_arr));
  test_session->SetUpCamera<scenic::StereoCamera>().SetStereoProjection(projection_arr,
                                                                        projection_arr);

  const float pane_width = viewport_width / 2;
  const float pane_height = viewport_height / 2;

  glm::vec3 translation(.5f * display_width, .5f * display_height, -10);

  scenic::Rectangle pane_shape(session, pane_width, pane_height);

  scenic::Material pane_material(session);
  pane_material.SetColor(255, 255, 255, 255);

  scenic::ShapeNode pane_shape_node(session);
  pane_shape_node.SetShape(pane_shape);
  pane_shape_node.SetMaterial(pane_material);
  pane_shape_node.SetTranslation(translation.x, translation.y, translation.z);
  test_session->scene.AddChild(pane_shape_node);

  Present(session);
  scenic::Screenshot screenshot = TakeScreenshot();

  // Color array to index 0=BLACK 1=WHITE
  scenic::Color colors[2] = {scenic::Color{0, 0, 0, 0}, scenic::Color{255, 255, 255, 255}};

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
      EXPECT_EQ(colors[expected[i][j]], screenshot.ColorAt(x, y))
          << "i = " << i << ", j = " << j << ", Sample Location: {" << x << ", " << y << "}";
    }
  }
}

// At a high level this test puts a camera inside a cube where each face is a
// different color, then uses a pose buffer to point the camera at different
// faces, using the colors to verify the pose buffer is working as expected.
TEST_F(ScenicPixelTest, PoseBuffer) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  scenic::Scene* const scene = &test_session->scene;

  const float viewport_width = display_width / 2;
  const float viewport_height = display_height;
  static constexpr float kCameraOffset = 500;
  // View matrix matches vulkan clip space +Y down, looking in direction of +Z
  const glm::vec3 eye(display_width / 2.f, display_height / 2.f, -kCameraOffset);
  const glm::vec3 look_at(eye + glm::vec3(0, 0, 1));
  const std::array<float, 3> up = {0, -1, 0};

  scenic::StereoCamera camera(test_session->scene);
  camera.SetTransform({eye.x, eye.y, eye.z}, {look_at.x, look_at.y, look_at.z}, up);

  glm::mat4 projection =
      glm::perspective(glm::radians(120.f), viewport_width / viewport_height, 0.1f, kCameraOffset);
  // projection = glm::scale(projection, glm::vec3(1.f, -1.f, 1.f));

  // clang-format off
  glm::mat4 clip(1.0f,  0.0f, 0.0f, 0.0f,
                 0.0f, -1.0f, 0.0f, 0.0f,
                 0.0f,  0.0f, 0.5f, 0.0f,
                 0.0f,  0.0f, 0.5f, 1.0f);
  // clang-format on
  projection = clip * projection;

  std::array<float, 16> projection_arr;
  auto projection_ptr = glm::value_ptr(projection);
  std::copy(projection_ptr, projection_ptr + 16, std::begin(projection_arr));
  camera.SetStereoProjection(projection_arr, projection_arr);

  test_session->renderer.SetCamera(camera.id());

  // Configure PoseBuffer

  const size_t kVmoSize = PAGE_SIZE;
  zx_status_t status;

  auto vulkan_queues = scenic_impl::gfx::test::VkSessionTest::CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  // TODO(SCN-1369): Scenic may use a different set of bits when creating a
  // buffer, resulting in a memory pool mismatch.
  const vk::BufferUsageFlags kUsageFlags =
      vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
      vk::BufferUsageFlagBits::eStorageTexelBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
      vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer;

  auto memory_requirements =
      scenic_impl::gfx::test::GetBufferRequirements(device, kVmoSize, kUsageFlags);
  auto memory = scenic_impl::gfx::test::AllocateExportableMemory(
      device, physical_device, memory_requirements,
      vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCached);

  // If we can't make memory that is both host-visible and device-local, we
  // can't run this test.
  if (!memory) {
    FX_LOGS(INFO) << "Could not find UMA compatible memory pool, aborting test.";
    return;
  }

  zx::vmo pose_buffer_vmo =
      scenic_impl::gfx::test::ExportMemoryAsVmo(device, vulkan_queues->dispatch_loader(), memory);

  zx::vmo remote_vmo;
  status = pose_buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &remote_vmo);
  FX_CHECK(status == ZX_OK);

  zx::time base_time = zx::clock::get_monotonic();
  // Normally the time interval is the period of time between each entry in the
  // pose buffer. In this example we only use one entry so the time interval is
  // pretty meaningless. Set to 1 for simplicity (see ARGO-21).
  zx::duration time_interval(1);
  uint32_t num_entries = 1;

  scenic::Memory mem(session, std::move(remote_vmo), kVmoSize,
                     fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  scenic::Buffer pose_buffer(mem, 0, kVmoSize);

  camera.SetPoseBuffer(pose_buffer, num_entries, base_time, time_interval);

  // Set up scene.

  static constexpr float kPaneWidth = kCameraOffset / 2.f;
  scenic::Rectangle pane_shape(session, kPaneWidth, kPaneWidth);

  static const int num_panes = 6;

  scenic::Color colors[num_panes] = {
      scenic::Color({255, 0, 0, 255}),    // RED
      scenic::Color({0, 255, 255, 255}),  // CYAN
      scenic::Color({0, 255, 0, 255}),    // GREEN
      scenic::Color({255, 0, 255, 255}),  // MAGENTA
      scenic::Color({0, 0, 255, 255}),    // BLUE
      scenic::Color({255, 255, 0, 255}),  // YELLOW
  };

  static constexpr float kPaneOffset = kPaneWidth / 2;

  glm::vec3 translations[num_panes] = {
      eye + glm::vec3(0, 0, kPaneOffset),   // In front of camera.
      eye + glm::vec3(0, 0, -kPaneOffset),  // Behind camera.
      eye + glm::vec3(-kPaneOffset, 0, 0),  // Left of Camera
      eye + glm::vec3(kPaneOffset, 0, 0),   // Right of camera
      eye + glm::vec3(0, -kPaneOffset, 0),  // Above Camera
      eye + glm::vec3(0, kPaneOffset, 0),   // Below Camera
  };

  glm::quat orientations[num_panes] = {
      glm::quat(),  // identity quaternion
      glm::angleAxis(kPi, glm::vec3(1, 0, 0)),
      glm::angleAxis(-kPi / 2, glm::vec3(0, 1, 0)),
      glm::angleAxis(kPi / 2, glm::vec3(0, 1, 0)),
      glm::angleAxis(kPi / 2, glm::vec3(1, 0, 0)),
      glm::angleAxis(-kPi / 2, glm::vec3(1, 0, 0)),
  };

  for (int i = 0; i < num_panes; i++) {
    scenic::Color color = colors[i];
    glm::vec3 translation = translations[i];
    glm::quat orientation = orientations[i];

    FX_LOGS(ERROR) << "translation: " << glm::to_string(translation);
    FX_LOGS(ERROR) << "orientation: " << glm::to_string(orientation);

    scenic::Material pane_material(session);
    pane_material.SetColor(color.r, color.g, color.b, color.a);
    scenic::ShapeNode pane_shape_node(session);
    pane_shape_node.SetShape(pane_shape);
    pane_shape_node.SetMaterial(pane_material);
    pane_shape_node.SetTranslation(translation.x, translation.y, translation.z);
    pane_shape_node.SetRotation(orientation.x, orientation.y, orientation.z, orientation.w);
    scene->AddChild(pane_shape_node);
  }

  static const int num_quaternions = 8;

  glm::quat quaternions[num_quaternions] = {
      glm::quat(),                                   // dead ahead
      glm::angleAxis(kPi, glm::vec3(0, 0, 1)),       // dead ahead but upside down
      glm::angleAxis(kPi, glm::vec3(1, 0, 0)),       // behind around X
      glm::angleAxis(kPi, glm::vec3(0, 1, 0)),       // behind around Y
      glm::angleAxis(kPi / 2, glm::vec3(0, 1, 0)),   // left
      glm::angleAxis(-kPi / 2, glm::vec3(0, 1, 0)),  // right
      glm::angleAxis(kPi / 2, glm::vec3(1, 0, 0)),   // up
      glm::angleAxis(-kPi / 2, glm::vec3(1, 0, 0)),  // down
  };

  int expected_color_index[num_quaternions] = {0, 0, 1, 1, 2, 3, 4, 5};

  uintptr_t ptr;
  status = zx::vmar::root_self()->map(0, pose_buffer_vmo, 0, kVmoSize,
                                      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &ptr);
  FX_CHECK(status == ZX_OK);

  auto pose_buffer_ptr = reinterpret_cast<escher::hmd::Pose*>(ptr);

  for (int i = 0; i < num_quaternions; i++) {
    // Put pose into pose buffer.
    // Only testing orientation so position is always the origin.
    // Quaternion describes head orientation, so invert it to get a transform
    // that takes you into head space.
    escher::hmd::Pose pose(glm::inverse(quaternions[i]), glm::vec3(0, 0, 0));

    *pose_buffer_ptr = pose;

    // Manually flush the buffer so this works on ARM
    status = pose_buffer_vmo.op_range(ZX_VMO_OP_CACHE_CLEAN, 0, kVmoSize, nullptr, 0);
    FX_CHECK(status == ZX_OK);

    Present(session);

    EXPECT_EQ(colors[expected_color_index[i]], TakeScreenshot().ColorAt(0.25, 0.5)) << "i = " << i;
  }
  device.freeMemory(memory);
}

struct OpacityTestParams {
  float opacity;
  scenic::Color expected_color;
};

class ParameterizedOpacityPixelTest : public ScenicPixelTest,
                                      public ::testing::WithParamInterface<OpacityTestParams> {};

TEST_P(ParameterizedOpacityPixelTest, CheckPixels) {
  constexpr auto COMPARE_COLOR = [](const scenic::Color& color_1, const scenic::Color& color_2,
                                    int max_error) {
    EXPECT_TRUE(abs(color_1.r - color_2.r) <= max_error &&
                abs(color_1.g - color_2.g) <= max_error &&
                abs(color_1.b - color_2.b) <= max_error && abs(color_1.a - color_2.a) <= max_error)
        << "Color " << color_1 << " and " << color_2 << " don't match.";
  };

  OpacityTestParams test_params = GetParam();

  scenic::OpacityView view(CreatePresentationContext());

  view.set_background_color(0xff, 0x00, 0xf0);
  view.set_foreground_color(0x00, 0xff, 0x0f);
  view.set_foreground_opacity(test_params.opacity);

  RunUntilIndirectPresent(&view);
  scenic::Screenshot screenshot = TakeScreenshot();
  ASSERT_FALSE(screenshot.empty());

  // We could assert on each pixel individually, but a histogram might give us
  // a more meaningful failure.
  std::map<scenic::Color, size_t> histogram = screenshot.Histogram();

  // There should be only one color here in the histogram.
  COMPARE_COLOR(histogram.begin()->first, test_params.expected_color, 1);
}

// We use the same background/foreground color for each test iteration, but
// vary the opacity.  When the opacity is 0% we expect the pure background
// color, and when it is 100% we expect the pure foreground color.  When
// opacity is 50% we expect a blend of the two.
INSTANTIATE_TEST_SUITE_P(
    Opacity, ParameterizedOpacityPixelTest,
    ::testing::Values(
        OpacityTestParams{.opacity = 0.0f, .expected_color = {0xff, 0x00, 0xf0, 0xff}},
        OpacityTestParams{.opacity = 0.5f, .expected_color = {0xbb, 0xbb, 0xb1, 0xff}},
        OpacityTestParams{.opacity = 1.0f, .expected_color = {0x00, 0xff, 0x0f, 0xff}}));

TEST_F(ScenicPixelTest, ViewBoundClipping) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;

  test_session->SetUpCamera().SetProjection(0);

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  scenic::View view(session, std::move(view_token), "ClipView");
  scenic::ViewHolder view_holder(session, std::move(view_holder_token), "ClipViewHolder");

  static const std::array<float, 3> bmin = {0.f, 0.f, -2.f};
  const std::array<float, 3> bmax = {display_width / 2, display_height, 1.f};
  static const std::array<float, 3> imin = {0, 0, 0};
  static const std::array<float, 3> imax = {0, 0, 0};
  view_holder.SetViewProperties(bmin, bmax, imin, imax);

  // Pane extends all the way across the screen horizontally, but
  // the view is only on the left-hand side of the screen.
  int32_t pane_width = display_width;
  int32_t pane_height = 0.25 * display_height;
  scenic::Rectangle pane_shape(session, pane_width, pane_height);
  scenic::Material pane_material(session);
  pane_material.SetColor(255, 0, 255, 255);  // Magenta.

  scenic::ShapeNode pane_node(session);
  pane_node.SetShape(pane_shape);
  pane_node.SetMaterial(pane_material);
  pane_node.SetTranslation(0.5 * pane_width, 0.5 * display_height, 0);

  // Second pane node should be completely outside the view bounds
  // along the z-axis and get clipped entirely.
  scenic::ShapeNode pane_node2(session);
  pane_node2.SetShape(scenic::Rectangle(session, pane_width, pane_height));
  scenic::Material pane_material2(session);
  pane_material2.SetColor(0, 255, 255, 255);  // Another color.
  pane_node2.SetMaterial(pane_material2);
  pane_node2.SetTranslation(0.5 * pane_width, display_height - 0.5 * pane_height, 3);

  test_session->scene.AddChild(view_holder);
  view.AddChild(pane_node);
  view.AddChild(pane_node2);

  Present(session);
  scenic::Screenshot screenshot = TakeScreenshot();

  scenic::Color unclipped_color = screenshot.ColorAt(0.1, 0.5);
  scenic::Color clipped_color = screenshot.ColorAt(0.6, 0.5);
  scenic::Color clipped_color2 = screenshot.ColorAt(0.1, 0.95);

  // Unclipped color should be magenta, clipped should be black.
  EXPECT_EQ(unclipped_color, scenic::Color(255, 0, 255, 255));
  EXPECT_EQ(clipped_color, scenic::Color(0, 0, 0, 0));

  // For pane2, it should be black as well.
  EXPECT_EQ(clipped_color2, scenic::Color(0, 0, 0, 0));
}

// This unit test verifies the behavior of view bound clipping when the view exists under a node
// that itself has a translation applied to it. There are two views with a rectangle in each. The
// first view is under a node that is translated (display_width/2, 0,0). The second view is placed
// under the first transform node, and then translated again by (0, display_height/2, 0,0). This
// means that what you see on the screen should look like the following:
//
//  xxxxxxxxxxvvvvvvvvvv
//  xxxxxxxxxxvvvvvvvvvv
//  xxxxxxxxxxvvvvvvvvvv
//  xxxxxxxxxxvvvvvvvvvv
//  xxxxxxxxxxvvvvvvvvvv
//  xxxxxxxxxxrrrrrrrrrr
//  xxxxxxxxxxrrrrrrrrrr
//  xxxxxxxxxxrrrrrrrrrr
//  xxxxxxxxxxrrrrrrrrrr
//  xxxxxxxxxxrrrrrrrrrr
//
// Where x refers to empty display pixels.
//       v refers to pixels covered by the first view's bounds.
//       r refers to pixels covered by the second view's bounds.
//
// All of the view bounds are given in local coordinates (so their min-point is at (0,0) in the xy
// plane) which means the test would fail if the bounds were not being updated properly to the
// correct world-space location by the transform stack before rendering.
TEST_F(ScenicPixelTest, ViewBoundClippingWithTransforms) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;

  // Initialize second session
  auto unique_session_2 = std::make_unique<scenic::Session>(scenic());
  auto session2 = unique_session_2.get();
  session2->set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Session terminated.";
    QuitLoop();
  });

  // Initialize third session
  auto unique_session_3 = std::make_unique<scenic::Session>(scenic());
  auto session3 = unique_session_3.get();
  session3->set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Session terminated.";
    QuitLoop();
  });

  test_session->SetUpCamera().SetProjection(0);

  // Add a transform node anchored in the top-middle of the display
  // along the x-axis and at the top with respect to the y-axis.
  scenic::EntityNode transform_node(session);
  transform_node.SetTranslation(display_width / 2, 0, 0);

  // Add the transform node as a child of the scene.
  test_session->scene.AddChild(transform_node);

  // Create two sets of view/view-holder token pairs.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token_2, view_holder_token_2] = scenic::ViewTokenPair::New();

  scenic::View view(session2, std::move(view_token), "ClipView");
  scenic::ViewHolder view_holder(session, std::move(view_holder_token), "ClipViewHolder");

  scenic::View view2(session3, std::move(view_token_2), "ClipView2");
  scenic::ViewHolder view_holder2(session, std::move(view_holder_token_2), "ClipViewHolder2");

  // Bounds of each view should be the size of a quarter of the display with
  // origin at 0,0 relative to its transform node.
  const std::array<float, 3> bmin = {0.f, 0.f, -2.f};
  const std::array<float, 3> bmax = {display_width / 2, display_height / 2, 1.f};
  const std::array<float, 3> imin = {0, 0, 0};
  const std::array<float, 3> imax = {0, 0, 0};
  view_holder.SetViewProperties(bmin, bmax, imin, imax);
  view_holder2.SetViewProperties(bmin, bmax, imin, imax);

  view_holder2.SetTranslation(0, display_height / 2, 0);

  // Pane extends across the entire right-side of the display, even though
  // its containing view is only in the top-right corner.
  int32_t pane_width = display_width / 2;
  int32_t pane_height = display_height;
  scenic::Rectangle pane_shape(session2, pane_width, pane_height);
  scenic::Rectangle pane_shape2(session3, pane_width, pane_height);

  // Make two pane materials
  scenic::Material pane_material(session2);
  pane_material.SetColor(255, 0, 255, 255);  // Magenta.

  scenic::Material pane_material2(session3);
  pane_material2.SetColor(0, 255, 255, 255);  // Cyan

  scenic::ShapeNode pane_node(session2);
  pane_node.SetShape(pane_shape);
  pane_node.SetMaterial(pane_material);
  pane_node.SetTranslation(pane_width / 2, pane_height / 2, 0);

  scenic::ShapeNode pane_node2(session3);
  pane_node2.SetShape(pane_shape2);
  pane_node2.SetMaterial(pane_material2);

  // Pane node 2 improperly extends above view2's bounds in the y-axis,
  // overlapping with view1, but should still be clipped.
  pane_node2.SetTranslation(pane_width / 2, 0, 0);

  // Add view holders to the transform.
  transform_node.AddChild(view_holder);
  view.AddChild(pane_node);
  transform_node.AddChild(view_holder2);
  view2.AddChild(pane_node2);

  Present(session);
  Present(session2);
  Present(session3);

  scenic::Screenshot screenshot = TakeScreenshot();

  scenic::Color magenta_color = screenshot.ColorAt(0.6, 0.1);
  scenic::Color magenta_color2 = screenshot.ColorAt(0.9, 0.4);
  scenic::Color cyan_color = screenshot.ColorAt(0.6, 0.9);
  scenic::Color black_color = screenshot.ColorAt(0.0, 0.5);

  // Upper-right quadrant should be magenta, lower-right quadrant
  // should be cyan. The left half of the screen should be black.
  EXPECT_EQ(magenta_color, scenic::Color(255, 0, 255, 255));
  EXPECT_EQ(magenta_color2, scenic::Color(255, 0, 255, 255));
  EXPECT_EQ(cyan_color, scenic::Color(0, 255, 255, 255));
  EXPECT_EQ(black_color, scenic::Color(0, 0, 0, 0));
}

// Creates three views and renders their wireframe bounds.
// Looks like this:
//
// aaaaaaaaaabbbbbbbbbb
// a        ab        b
// a        ab        b
// a        abbbbbbbbbb
// a        acccccccccc
// a        ac        c
// a        ac        c
// aaaaaaaaaacccccccccc
//
// Where a,b, and c represent the bounds for views 1,2, and
// 3 respectively.
TEST_F(ScenicPixelTest, ViewBoundWireframeRendering) {
  auto escher = escher::test::GetEscher()->GetWeakPtr();
  bool supports_wireframe = escher->supports_wireframe();
  if (!supports_wireframe) {
    FX_LOGS(INFO) << "Vulkan device feature fillModeNonSolid is not supported on this device. "
                     "Error messages are expected.";
  }

  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  scenic::Scene* const scene = &test_session->scene;
  test_session->SetUpCamera().SetProjection(0);

  // Initialize session 2.
  auto unique_session2 = std::make_unique<scenic::Session>(scenic());
  auto session2 = unique_session2.get();
  session2->set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Session terminated.";
    QuitLoop();
  });

  // Initialize session 3.
  auto unique_session3 = std::make_unique<scenic::Session>(scenic());
  auto session3 = unique_session3.get();
  session3->set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Session terminated.";
    QuitLoop();
  });

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();
  auto [view_token3, view_holder_token3] = scenic::ViewTokenPair::New();

  scenic::View view(session, std::move(view_token), "ClipView");
  scenic::ViewHolder view_holder(session, std::move(view_holder_token), "ClipViewHolder");

  // View 2 is embedded by view 1.
  scenic::View view2(session2, std::move(view_token2), "ClipView2");
  scenic::ViewHolder view_holder2(session, std::move(view_holder_token2), "ClipViewHolder2");

  // View 3 is embedded by view 2 and thus doubly embedded within view 1.
  scenic::View view3(session3, std::move(view_token3), "ClipView3");
  scenic::ViewHolder view_holder3(session2, std::move(view_holder_token3), "ClipViewHolder3");

  const std::array<float, 3> bmin = {0.f, 0.f, -2.f};
  const std::array<float, 3> bmax = {display_width / 2, display_height, 1.f};
  const std::array<float, 3> imin = {1, 1, 0};
  const std::array<float, 3> imax = {1, 1, 0};
  view_holder.SetViewProperties(bmin, bmax, imin, imax);

  const std::array<float, 3> bmin2 = {0, 0, -2.f};
  const std::array<float, 3> bmax2 = {display_width / 2, display_height / 2, 1.f};
  view_holder2.SetViewProperties(bmin2, bmax2, imin, imax);
  view_holder3.SetViewProperties(bmin2, bmax2, imin, imax);

  // Set the debug bounds colors.
  view_holder.SetDebugBoundsColor(0, 255, 255);
  view_holder2.SetDebugBoundsColor(255, 0, 255);
  view_holder3.SetDebugBoundsColor(255, 255, 0);

  // Set bounds rendering on just the first view. This should turn on debug
  // wireframe for itself and view2, since view2 is a direct embedding. View3
  // should still be off.
  view.enableDebugBounds(true);

  scene->AddChild(view_holder);

  // Transform and embed view holder 2 in first view.
  scenic::EntityNode transform_node(session);
  transform_node.SetTranslation(display_width / 2, 0, 0);
  view.AddChild(transform_node);
  transform_node.AddChild(view_holder2);

  // Transform and embed view holder 3 in view 2.
  scenic::EntityNode transform_node2(session2);
  transform_node2.SetTranslation(0, display_height / 2, 0);
  view2.AddChild(transform_node2);
  transform_node2.AddChild(view_holder3);

  Present(session);
  Present(session2);
  Present(session3);

  // Take screenshot.
  scenic::Screenshot screenshot = TakeScreenshot();
  ASSERT_FALSE(screenshot.empty());
  auto histogram = screenshot.Histogram();

  if (supports_wireframe) {
    histogram.erase({0, 0, 0, 0});
    scenic::Color expected_colors[2] = {{0, 255, 255, 255},   // First ViewHolder
                                        {255, 0, 255, 255}};  // Second ViewHolder
    for (uint32_t i = 0; i < 2; i++) {
      EXPECT_GT(histogram[expected_colors[i]], 0u);
      histogram.erase(expected_colors[i]);
    }
    EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
  } else {
    // If drawing wireframe is not supported, there should be nothing displayed
    // on screen.
    histogram.erase({0, 0, 0, 0});
    EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
  }

  // Now toggle debug rendering for view 2. This should tirgger view3's bounds to
  // display as view3 is directly embedded by view2.
  view2.enableDebugBounds(true);

  Present(session);
  Present(session2);
  Present(session3);

  // Take screenshot.
  scenic::Screenshot screenshot2 = TakeScreenshot();
  ASSERT_FALSE(screenshot2.empty());
  histogram = screenshot2.Histogram();

  if (supports_wireframe) {
    histogram.erase({0, 0, 0, 0});
    scenic::Color expected_colors_2[3] = {{0, 255, 255, 255},   // First ViewHolder
                                          {255, 0, 255, 255},   // Second ViewHolder
                                          {255, 255, 0, 255}};  // Third ViewHolder
    for (uint32_t i = 0; i < 3; i++) {
      EXPECT_GT(histogram[expected_colors_2[i]], 0u);
      histogram.erase(expected_colors_2[i]);
    }
    EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
  } else {
    // If drawing wireframe is not supported, there should be nothing displayed
    // on screen.
    histogram.erase({0, 0, 0, 0});
    EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
  }
}

// TODO(SCN-1375): Blocked against hardware inability
// to provide accurate screenshots from the physical
// display. Our "TakeScreenshot()" method only grabs
// pixel data from Escher before it gets sent off to
// the display controller and thus cannot accurately
// capture color conversion information.
TEST_F(ScenicPixelTest, DISABLED_Compositor) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  scenic::Scene* const scene = &test_session->scene;

  test_session->SetUpCamera().SetProjection(0);

  // Color correction data
  static const std::array<float, 3> preoffsets = {0, 0, 0};
  static const std::array<float, 9> matrix = {.288299,  0.052709, -0.257912, 0.711701, 0.947291,
                                              0.257912, 0.000000, -0.000000, 1.000000};
  static const std::array<float, 3> postoffsets = {0, 0, 0};

  static const glm::mat4 glm_matrix(.288299, 0.052709, -0.257912, 0.00000, 0.711701, 0.947291,
                                    0.257912, 0.00000, 0.000000, -0.000000, 1.000000, 0.00000,
                                    0.000000, 0.000000, 0.00000, 1.00000);

  const float pane_width = display_width / 5;
  const float pane_height = display_height;

  static const float colors[15] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0};

  for (uint32_t i = 0; i < 5; i++) {
    scenic::Rectangle pane_shape(session, pane_width, pane_height);
    scenic::Material pane_material(session);
    pane_material.SetColor(255 * colors[3 * i], 255 * colors[3 * i + 1], 255 * colors[3 * i + 2],
                           255);

    scenic::ShapeNode pane_node(session);
    pane_node.SetShape(pane_shape);
    pane_node.SetMaterial(pane_material);
    pane_node.SetTranslation((i + 0.5) * pane_width, 0.5 * pane_height, -20);
    scene->AddChild(pane_node);
  }

  // Display uncorrected version first.
  Present(session);
  scenic::Screenshot prev_screenshot = TakeScreenshot();

  // Apply color correction.
  test_session->compositor.SetColorConversion(preoffsets, matrix, postoffsets);

  // Display color corrected version.
  Present(session, zx::time(1000000));
  scenic::Screenshot post_screenshot = TakeScreenshot();

  for (uint32_t i = 0; i < 5; i++) {
    scenic::Color prev_color = prev_screenshot.ColorAt(i * .2, 0.5);
    scenic::Color post_color = post_screenshot.ColorAt(i * .2, 0.5);

    glm::vec4 vec = glm_matrix * glm::vec4(prev_color.r, prev_color.g, prev_color.b, 1);
    scenic::Color res(vec.x, vec.y, vec.z, vec.w);
    EXPECT_EQ(res, post_color);
  }
}

// This test sets up a scene, takes a screenshot, rotates display configuration
// by 90 degrees and takes a second screenshot, then makes sure that the pixels
// in both screenshots map onto each other how you would expect.

class RotationTest : public ScenicPixelTest {
 public:
  void TestRotation(uint32_t angle) {
    auto test_session = SetUpTestSession();
    scenic::Session* const session = &test_session->session;
    const auto [display_width, display_height] = test_session->display_dimensions;
    scenic::Scene* const scene = &test_session->scene;

    test_session->SetUpCamera().SetProjection(0);

    const float pane_width = display_width / 5;
    const float pane_height = display_height;

    // For this test, create 5 vertical bands. This is an array of
    // the rgb colors for each of the five bands that will be
    // created below.
    static const float colors[15] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0};

    for (uint32_t i = 0; i < 5; i++) {
      scenic::Rectangle pane_shape(session, pane_width, pane_height);
      scenic::Material pane_material(session);
      pane_material.SetColor(255 * colors[3 * i], 255 * colors[3 * i + 1], 255 * colors[3 * i + 2],
                             255);

      scenic::ShapeNode pane_node(session);
      pane_node.SetShape(pane_shape);
      pane_node.SetMaterial(pane_material);
      pane_node.SetTranslation((i + 0.5) * pane_width, 0.5 * pane_height, -20);
      scene->AddChild(pane_node);
    }

    // Display unrotated version first.
    Present(session);
    scenic::Screenshot prev_screenshot = TakeScreenshot();

    test_session->compositor.SetLayoutRotation(angle);

    // Display rotated version.
    Present(session, zx::time(1000000));
    scenic::Screenshot post_screenshot = TakeScreenshot();

    // The pre and post width and height should be the reverse of each other.
    EXPECT_EQ(prev_screenshot.width(), post_screenshot.height());
    EXPECT_EQ(prev_screenshot.height(), post_screenshot.width());

    // All of the colors should be transposed.
    // Only support 90 and 270 degree rotations here.
    for (uint32_t x = 0; x < prev_screenshot.width(); x++) {
      for (uint32_t y = 0; y < prev_screenshot.height(); y++) {
        uint32_t post_x = angle == 90 ? y : prev_screenshot.height() - y - 1;
        uint32_t post_y = angle == 90 ? prev_screenshot.width() - x - 1 : x;

        EXPECT_EQ(prev_screenshot[y][x], post_screenshot[post_y][post_x]);
      }
    }
  }
};

TEST_F(RotationTest, Test90) { TestRotation(90); }

TEST_F(RotationTest, RotationTest270) { TestRotation(270); }

// Test to make sure scenic can properly render basic shapes like circles.
TEST_F(ScenicPixelTest, BasicShapeTest) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  scenic::Scene* const scene = &test_session->scene;

  test_session->SetUpCamera().SetProjection(0);

  const float kRadius = 10;

  scenic::Circle circle_shape(session, kRadius);
  scenic::Material circle_material(session);
  circle_material.SetColor(255, 0, 255, 255);

  scenic::ShapeNode circle_node(session);
  circle_node.SetShape(circle_shape);
  circle_node.SetMaterial(circle_material);
  circle_node.SetTranslation(display_width / 2, display_height / 2, -20);
  scene->AddChild(circle_node);

  Present(session);
  scenic::Screenshot screenshot = TakeScreenshot();
  EXPECT_EQ(screenshot.ColorAt(0.5, 0.5), scenic::Color(255, 0, 255, 255));
}

// This test zooms in on the lower-right quadrant and verifies that only that is
// shown.
TEST_F(ScenicPixelTest, ClipSpaceTransformOrtho) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  scenic::Scene* const scene = &test_session->scene;

  struct Shape {
    float scale;
    scenic::Color color;
    glm::vec3 translation;
  };

  // clang-format off
  static const std::array<Shape, 3> shapes {
    Shape {
      .scale = 1,
      .color = {255, 0, 0, 255},
      .translation = {.5f, .5f, -10}
    },
    Shape {
      .scale = .5f,
      .color = {0, 255, 0, 255},
      .translation = {.75f, .75f, -20}
    },
    Shape {
      .scale = .4f,
      .color = {0, 0, 255, 255},
      .translation = {.75f, .75f, -30}
    }
  };
  // clang-format on

  for (const auto& shape : shapes) {
    scenic::Rectangle rectangle(session, shape.scale * display_width, shape.scale * display_height);
    scenic::Material material(session);
    material.SetColor(shape.color.r, shape.color.g, shape.color.b, shape.color.a);

    scenic::ShapeNode node(session);
    node.SetShape(rectangle);
    node.SetMaterial(material);
    node.SetTranslation(shape.translation.x * display_width, shape.translation.y * display_height,
                        shape.translation.z);
    scene->AddChild(node);
  }

  auto camera = test_session->SetUpCamera();
  camera.SetProjection(0);
  camera.SetClipSpaceTransform(-1, -1, 2);

  Present(session);
  scenic::Screenshot screenshot = TakeScreenshot();

  std::map<scenic::Color, size_t> histogram = screenshot.Histogram();
  EXPECT_EQ(histogram[shapes[0].color], 0u);
  EXPECT_GT(histogram[shapes[1].color], 0u);
  EXPECT_GT(histogram[shapes[2].color], histogram[shapes[1].color]);
}

// This test ensures that clip-space transforms do not distort the projection
// matrix by setting up a scene that contains a splitting plane that should not
// show up in perspective (aligned with the view vector, centered) but would if
// the camera were naively translated.
//
// Viewed from above, the scene looks like this:
//  bad good
//  \  b  /
//  ?\ a /?
//  ??\d/??
//    cam
//      zoom (2x, right side)
TEST_F(ScenicPixelTest, ClipSpaceTransformPerspective) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  scenic::Scene* const scene = &test_session->scene;

  static const glm::quat face_right = glm::angleAxis(kPi / 2, glm::vec3(0, -1, 0));
  static const float kFovy = kPi / 4;
  static const float background_height =
      2 * tan(kFovy / 2) * gfx::TestSession::kDefaultCameraOffset;
  const float background_width = background_height / display_height * display_width;

  struct Shape {
    scenic::Color color;
    glm::vec2 size;
    glm::vec3 translation;
    const glm::quat* rotation;
  };

  // clang-format off
  static const std::array<Shape, 3> shapes {
    Shape {
      .color = {255, 0, 0, 255},
      .size = {background_width / 2, background_height},
      .translation = {-background_width / 4, 0, -10},
      .rotation = nullptr
    },
    Shape {
      .color = {0, 255, 0, 255},
      .size = {background_width / 2, background_height},
      .translation = {background_width / 4, 0, -10},
      .rotation = nullptr
    },
    Shape {
      .color = {0, 0, 255, 255},
      // SCN-1276: The depth of the viewing volume is 1000.
      .size = {1000, background_height},
      .translation = {0, 0, -500},
      .rotation = &face_right
    }
  };
  // clang-format on

  for (const auto& shape : shapes) {
    scenic::Rectangle rectangle(session, shape.size.x, shape.size.y);
    scenic::Material material(session);
    material.SetColor(shape.color.r, shape.color.g, shape.color.b, shape.color.a);

    scenic::ShapeNode node(session);
    node.SetShape(rectangle);
    node.SetMaterial(material);
    node.SetTranslation(shape.translation.x + display_width / 2,
                        shape.translation.y + display_height / 2, shape.translation.z);
    if (shape.rotation) {
      node.SetRotation(shape.rotation->x, shape.rotation->y, shape.rotation->z, shape.rotation->w);
    }
    scene->AddChild(node);
  }

  auto camera = test_session->SetUpCamera();
  camera.SetProjection(kFovy);
  camera.SetClipSpaceTransform(-1, 0, 2);

  Present(session);
  scenic::Screenshot screenshot = TakeScreenshot();

  std::map<scenic::Color, size_t> histogram = screenshot.Histogram();
  EXPECT_EQ(histogram[shapes[0].color], 0u);
  EXPECT_EQ(histogram[shapes[2].color], 0u);
  EXPECT_GT(histogram[shapes[1].color], 0u);
}

class ParameterizedYuvPixelTest
    : public ScenicPixelTest,
      public ::testing::WithParamInterface<fuchsia::sysmem::PixelFormatType> {};

// Test that exercies sampling from YUV textures.
TEST_P(ParameterizedYuvPixelTest, YuvImagesOnImagePipe2) {
  auto escher_ptr = escher::test::GetEscher()->GetWeakPtr();
  if (!escher_ptr->device()->caps().allow_ycbcr) {
    FX_LOGS(WARNING) << "YUV images not supported. Test skipped.";
    GTEST_SKIP();
  }

  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  test_session->SetUpCamera().SetProjection(0);

  uint32_t next_id = session->next_resource_id();
  fuchsia::images::ImagePipe2Ptr image_pipe;
  const uint32_t kImagePipeId = next_id++;
  session->Enqueue(scenic::NewCreateImagePipe2Cmd(kImagePipeId, image_pipe.NewRequest()));
  const uint32_t kMaterialId = next_id++;
  session->Enqueue(scenic::NewCreateMaterialCmd(kMaterialId));
  session->Enqueue(scenic::NewSetTextureCmd(kMaterialId, kImagePipeId));
  const uint32_t kShapeNodeId = next_id++;
  session->Enqueue(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
  session->Enqueue(scenic::NewSetMaterialCmd(kShapeNodeId, kMaterialId));
  const uint32_t kShapeId = next_id++;
  session->Enqueue(scenic::NewCreateRectangleCmd(kShapeId, display_width, display_height));
  session->Enqueue(scenic::NewSetShapeCmd(kShapeNodeId, kShapeId));
  session->Enqueue(
      scenic::NewSetTranslationCmd(kShapeNodeId, {display_width * 0.5f, display_height * 0.5f, 0}));
  session->Enqueue(scenic::NewAddChildCmd(test_session->scene.id(), kShapeNodeId));
  Present(session);

  const uint32_t kShapeWidth = 32;
  const uint32_t kShapeHeight = 32;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  EXPECT_EQ(status, ZX_OK);

  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  status = local_token->Sync();
  EXPECT_EQ(status, ZX_OK);
  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(dup_token));

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                  buffer_collection.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWriteOften;

  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = GetParam();
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] =
      fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::REC709};
  image_constraints.min_coded_width = kShapeWidth;
  image_constraints.max_coded_width = kShapeWidth;
  image_constraints.min_coded_height = kShapeHeight;
  image_constraints.max_coded_height = kShapeHeight;
  image_constraints.max_bytes_per_row = kShapeWidth;

  status = buffer_collection->SetConstraints(true, constraints);
  EXPECT_EQ(status, ZX_OK);
  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  EXPECT_EQ(status, ZX_OK);
  // TODO(fxbug.dev/54153): This test is skipped on FEMU until we support external
  // host-visible image allocation on FEMU.
  if (allocation_status == ZX_ERR_NOT_SUPPORTED) {
    FX_LOGS(WARNING) << "Buffer constraints not supported. Test skipped.";
    GTEST_SKIP();
  } else {
    ASSERT_EQ(ZX_OK, allocation_status);
  }
  EXPECT_FALSE(buffer_collection_info.settings.buffer_settings.is_secure);
  status = buffer_collection->Close();
  EXPECT_EQ(status, ZX_OK);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kShapeWidth;
  image_format.coded_height = kShapeHeight;
  const uint32_t kImageId = 1;
  image_pipe->AddImage(kImageId, kBufferId, 0, image_format);

  uint8_t* vmo_base;
  const zx::vmo& image_vmo = buffer_collection_info.buffers[0].vmo;
  auto image_vmo_bytes = buffer_collection_info.settings.buffer_settings.size_bytes;
  EXPECT_GT(image_vmo_bytes, 0u);

  status = zx::vmar::root_self()->map(0, image_vmo, 0, image_vmo_bytes,
                                      ZX_VM_PERM_WRITE | ZX_VM_PERM_READ,
                                      reinterpret_cast<uintptr_t*>(&vmo_base));
  vmo_base += buffer_collection_info.buffers[0].vmo_usable_start;
  const uint32_t num_pixels = kShapeWidth * kShapeHeight;
  static const uint8_t kYValue = 110;
  static const uint8_t kUValue = 192;
  static const uint8_t kVValue = 192;
  const scenic::Color kBgraColor = {0xF1, 0x87, 0xFA, 0xFF};

  for (uint32_t i = 0; i < num_pixels; ++i) {
    vmo_base[i] = kYValue;
  }
  switch (GetParam()) {
    case fuchsia::sysmem::PixelFormatType::NV12:
      for (uint32_t i = num_pixels; i < num_pixels + num_pixels / 2; i += 2) {
        vmo_base[i] = kUValue;
        vmo_base[i + 1] = kVValue;
      }
      break;
    case fuchsia::sysmem::PixelFormatType::I420:
      for (uint32_t i = num_pixels; i < num_pixels + num_pixels / 4; ++i) {
        vmo_base[i] = kUValue;
      }
      for (uint32_t i = num_pixels + num_pixels / 4; i < num_pixels + num_pixels / 2; ++i) {
        vmo_base[i] = kVValue;
      }
      break;
    default:
      FX_NOTREACHED();
  }
  if (buffer_collection_info.settings.buffer_settings.coherency_domain ==
      fuchsia::sysmem::CoherencyDomain::RAM) {
    EXPECT_EQ(ZX_OK, buffer_collection_info.buffers[0].vmo.op_range(ZX_VMO_OP_CACHE_CLEAN, 0,
                                                                    image_vmo_bytes, nullptr, 0));
  }

  bool image_presented = false;
  image_pipe->PresentImage(
      kImageId, zx_clock_get_monotonic(), std::vector<zx::event>(), std::vector<zx::event>(),
      [&image_presented](fuchsia::images::PresentationInfo pinfo) { image_presented = true; });
  // Ensure an image with contents will be presented to the screen.
  EXPECT_TRUE(
      RunLoopWithTimeoutOrUntil([&image_presented] { return image_presented; }, zx::sec(15)));

  scenic::Screenshot screenshot = TakeScreenshot();
  ASSERT_FALSE(screenshot.empty());

  // Check that all pixels have the expected color.
  std::map<scenic::Color, size_t> histogram = screenshot.Histogram();
  EXPECT_GT(histogram[kBgraColor], 0u);
  histogram.erase(kBgraColor);
  EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
}

INSTANTIATE_TEST_SUITE_P(YuvPixelFormats, ParameterizedYuvPixelTest,
                         ::testing::Values(fuchsia::sysmem::PixelFormatType::NV12,
                                           fuchsia::sysmem::PixelFormatType::I420));

// We cannot capture protected content, so we expect a black screenshot instead.
TEST_F(ScenicPixelTest, ProtectedImage) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  test_session->SetUpCamera().SetProjection(0);

  fuchsia::images::ImagePipe2Ptr image_pipe;
  image_pipe.set_error_handler([](zx_status_t status) { GTEST_FAIL() << "ImagePipe terminated."; });
  const uint32_t kImagePipeId = session->next_resource_id();
  session->Enqueue(scenic::NewCreateImagePipe2Cmd(kImagePipeId, image_pipe.NewRequest()));

  const uint32_t kMaterialId = kImagePipeId + 1;
  session->Enqueue(scenic::NewCreateMaterialCmd(kMaterialId));
  session->Enqueue(scenic::NewSetTextureCmd(kMaterialId, kImagePipeId));

  const uint32_t kShapeNodeId = kMaterialId + 1;
  session->Enqueue(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
  session->Enqueue(scenic::NewSetMaterialCmd(kShapeNodeId, kMaterialId));
  const uint32_t kShapeId = kShapeNodeId + 1;
  session->Enqueue(scenic::NewCreateRectangleCmd(kShapeId, display_width, display_height));
  session->Enqueue(scenic::NewSetShapeCmd(kShapeNodeId, kShapeId));
  session->Enqueue(scenic::NewAddChildCmd(test_session->scene.id(), kShapeNodeId));
  Present(session);

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  status = local_token->Sync();
  EXPECT_EQ(status, ZX_OK);

  ASSERT_TRUE(image_pipe.is_bound());
  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(dup_token));
  // WaitForBuffersAllocated() hangs if AddBufferCollection() isn't finished successfully.
  RunLoopUntilIdle();

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                  buffer_collection.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.secure_required = true;
  constraints.buffer_memory_constraints.inaccessible_domain_supported = true;
  constraints.buffer_memory_constraints.cpu_domain_supported = false;
  constraints.buffer_memory_constraints.ram_domain_supported = false;
  constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageTransferSrc;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] =
      fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
  status = buffer_collection->SetConstraints(true, constraints);
  EXPECT_EQ(status, ZX_OK);
  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  if (allocation_status != ZX_OK) {
    // Protected memory might not be available in some devices which causes allocation failure.
    GTEST_SKIP() << "Protected memory cannot be allocated";
  }
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(buffer_collection_info.settings.buffer_settings.is_secure);
  EXPECT_EQ(
      0u,
      fsl::GetObjectName(buffer_collection_info.buffers[0].vmo.get()).find("ImagePipe2Surface"));
  status = buffer_collection->Close();
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = 1;
  image_format.coded_height = 1;
  const uint32_t kImageId = 1;
  image_pipe->AddImage(kImageId, kBufferId, 0, image_format);
  Present(session);

  scenic::Screenshot screenshot = TakeScreenshot();
  ASSERT_FALSE(screenshot.empty());
  EXPECT_EQ(scenic::Color({0, 0, 0, 255}), screenshot.ColorAt(.25f, .25f));
}

// Flaking on bots. TODO(fxbug.dev/42892): Re-enable. Add all supported pixel formats as test cases.
TEST_F(ScenicPixelTest, DISABLED_LinearImagePipe) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  test_session->SetUpCamera().SetProjection(0);

  fuchsia::images::ImagePipe2Ptr image_pipe;
  const uint32_t kImagePipeId = session->next_resource_id();
  session->Enqueue(scenic::NewCreateImagePipe2Cmd(kImagePipeId, image_pipe.NewRequest()));

  const uint32_t kMaterialId = kImagePipeId + 1;
  session->Enqueue(scenic::NewCreateMaterialCmd(kMaterialId));
  session->Enqueue(scenic::NewSetTextureCmd(kMaterialId, kImagePipeId));

  const uint32_t kShapeNodeId = kMaterialId + 1;
  session->Enqueue(scenic::NewCreateShapeNodeCmd(kShapeNodeId));
  session->Enqueue(scenic::NewSetMaterialCmd(kShapeNodeId, kMaterialId));
  const uint32_t kShapeId = kShapeNodeId + 1;
  session->Enqueue(scenic::NewCreateRectangleCmd(kShapeId, display_width, display_height));
  session->Enqueue(scenic::NewSetShapeCmd(kShapeNodeId, kShapeId));
  session->Enqueue(scenic::NewAddChildCmd(test_session->scene.id(), kShapeNodeId));
  Present(session);

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  status = local_token->Sync();
  EXPECT_EQ(status, ZX_OK);
  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(dup_token));

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                  buffer_collection.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWriteOften;

  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] =
      fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;
  image_constraints.required_max_coded_width = 1;
  image_constraints.required_max_coded_height = 1;

  status = buffer_collection->SetConstraints(true, constraints);
  EXPECT_EQ(status, ZX_OK);
  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  EXPECT_EQ(ZX_OK, allocation_status);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_FALSE(buffer_collection_info.settings.buffer_settings.is_secure);
  status = buffer_collection->Close();
  EXPECT_EQ(status, ZX_OK);

  // R=255 G=0 B=255 in BGRA32
  constexpr uint32_t kColor = 0xffff00ff;
  uint32_t kPixelSize = 4;
  EXPECT_EQ(ZX_OK, buffer_collection_info.buffers[0].vmo.write(&kColor, 0, kPixelSize));
  if (buffer_collection_info.settings.buffer_settings.coherency_domain ==
      fuchsia::sysmem::CoherencyDomain::RAM) {
    EXPECT_EQ(ZX_OK, buffer_collection_info.buffers[0].vmo.op_range(ZX_VMO_OP_CACHE_CLEAN, 0,
                                                                    kPixelSize, nullptr, 0));
  }
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = 1;
  image_format.coded_height = 1;
  const uint32_t kImageId = 1;
  image_pipe->AddImage(kImageId, kBufferId, 0, image_format);
  image_pipe->PresentImage(kImageId, zx_clock_get_monotonic(), std::vector<zx::event>(),
                           std::vector<zx::event>(),
                           [this](fuchsia::images::PresentationInfo pinfo) { this->QuitLoop(); });
  // Ensure an image with contents will be presented to the screen.
  ASSERT_FALSE(RunLoopWithTimeout(zx::sec(15)));
  Present(session);

  scenic::Screenshot screenshot = TakeScreenshot();
  ASSERT_FALSE(screenshot.empty());
  EXPECT_EQ(scenic::Color({255, 0, 255, 255}), screenshot.ColorAt(.25f, .25f));
}

// This test ensures that detaching a view holder ceases rendering the view. Finer grained
// functionality is covered in node and view unit tests.
TEST_F(ScenicPixelTest, ViewHolderDetach) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;

  test_session->SetUpCamera().SetProjection(0);

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  scenic::View view(session, std::move(view_token), "View");
  scenic::ViewHolder view_holder(session, std::move(view_holder_token), "ViewHolder");

  view_holder.SetViewProperties({.bounding_box = {
                                     .min = {0, 0, -2},
                                     .max = {display_width, display_height, 1},
                                 }});

  // Solid color
  scenic::Rectangle pane_shape(session, display_width, display_height);
  scenic::Material pane_material(session);
  pane_material.SetColor(255, 0, 255, 255);  // Magenta.

  scenic::ShapeNode pane_node(session);
  pane_node.SetShape(pane_shape);
  pane_node.SetMaterial(pane_material);
  pane_node.SetTranslation(display_width / 2, display_height / 2, 0);

  test_session->scene.AddChild(view_holder);
  view.AddChild(pane_node);

  Present(session);
  EXPECT_EQ(TakeScreenshot().ColorAt(.5f, .5f), scenic::Color(255, 0, 255, 255));  // Magenta

  view_holder.Detach();

  Present(session);
  EXPECT_EQ(TakeScreenshot().ColorAt(.5f, .5f), scenic::Color(0, 0, 0, 0));  // Blank
}

// This test case tests if Scenic can generate and present external GPU images correctly without
// causing any Vulkan validation errors (fxb/35652).
//
// This test first creates an escher Image and GPU memory bound to it, and uploaded plain color
// pixels (#FF8000) to that image. Then we export image as a vmo object, create that image using vmo
// directly in Scenic, and present that image.
//
// The image layout type should be correctly converted from eUndefined or ePreinitialized to any
// other valid type when it is presented. Otherwise this test will crash due to validation errors in
// gfx system.
VK_TEST_F(ScenicPixelTest, UseExternalImage) {
  constexpr size_t kImageSize = 256;
  constexpr scenic::Color kImageColor = {255, 128, 0, 255};

  scenic::BackgroundView view(CreatePresentationContext());
  auto escher_ptr = escher::test::GetEscher()->GetWeakPtr();
  auto uploader = escher::BatchGpuUploader::New(escher_ptr);

  // Create a RGBA (8-bit channels) images to write to.
  escher::ImageInfo image_info = {
      .format = vk::Format::eB8G8R8A8Unorm,
      .width = kImageSize,
      .height = kImageSize,
      .sample_count = 1,
      .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst |
               vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
      .memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal,
      .tiling = vk::ImageTiling::eOptimal,
      .is_mutable = true,
      .is_external = true};
  auto [gpu_mem_ptr, image] = escher::GenerateExportableMemImage(
      escher_ptr->vk_device(), escher_ptr->resource_recycler(), image_info);
  ASSERT_NE(gpu_mem_ptr.get(), nullptr);

  // Create and upload pixels to the escher Image.
  auto pixels = std::vector<uint8_t>(image_info.height * image_info.width * 4U);
  for (uint32_t i = 0; i < image_info.height * image_info.width; ++i) {
    pixels[i * 4] = kImageColor.b;
    pixels[i * 4 + 1] = kImageColor.g;
    pixels[i * 4 + 2] = kImageColor.r;
    pixels[i * 4 + 3] = kImageColor.a;
  }

  // Write the pixels generated above to escher Image.
  escher::image_utils::WritePixelsToImage(uploader.get(), pixels.data(), image);
  uploader->Submit();
  escher_ptr->vk_device().waitIdle();

  // Export the escher image as vmo for GpuImage creation.
  zx::vmo image_vmo = escher::ExportMemoryAsVmo(escher_ptr.get(), gpu_mem_ptr);
  uint64_t vmo_size = 0;
  auto get_size_result = image_vmo.get_size(&vmo_size);
  ASSERT_TRUE(get_size_result == ZX_OK);

  // Create a GPU image using the vmo exported above.
  fuchsia::images::ImageInfo fx_image_info{
      .width = kImageSize,
      .height = kImageSize,
      .stride = static_cast<uint32_t>(
          kImageSize * images::StrideBytesPerWidthPixel(fuchsia::images::PixelFormat::BGRA_8)),
      .pixel_format = fuchsia::images::PixelFormat::BGRA_8,
      .tiling = fuchsia::images::Tiling::GPU_OPTIMAL,
  };

  // Present the external GPU image using BackgroundView.
  view.SetImage(std::move(image_vmo), vmo_size, fx_image_info,
                fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  RunUntilIndirectPresent(&view);

  scenic::Screenshot screenshot = TakeScreenshot();
  ASSERT_FALSE(screenshot.empty());

  std::map<scenic::Color, size_t> histogram = screenshot.Histogram();

  EXPECT_GT(histogram[kImageColor], 0u);
  histogram.erase(kImageColor);

  // This assert is written this way so that, when it fails, it prints out all
  // the unexpected colors
  EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
}

VK_TEST_F(ScenicPixelTest, UseExternalImageImmutableRgba) {
  constexpr size_t kImageSize = 256;
  constexpr scenic::Color kImageColor = {255, 128, 0, 255};

  scenic::BackgroundView view(CreatePresentationContext());
  auto escher_ptr = escher::test::GetEscher()->GetWeakPtr();
  auto uploader = escher::BatchGpuUploader::New(escher_ptr);

  // Create a RGBA (8-bit channels) images to write to.
  escher::ImageInfo image_info = {
      // SRGB is required for immutable external images.
      .format = vk::Format::eR8G8B8A8Srgb,
      .width = kImageSize,
      .height = kImageSize,
      .sample_count = 1,
      .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst |
               vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
      .memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal,
      .tiling = vk::ImageTiling::eOptimal,
      .is_mutable = false,
      .is_external = true};
  auto [gpu_mem_ptr, image] = escher::GenerateExportableMemImage(
      escher_ptr->vk_device(), escher_ptr->resource_recycler(), image_info);
  ASSERT_NE(gpu_mem_ptr.get(), nullptr);

  // Create and upload pixels to the escher Image.
  auto pixels = std::vector<uint8_t>(image_info.height * image_info.width * 4U);
  for (uint32_t i = 0; i < image_info.height * image_info.width; ++i) {
    pixels[i * 4] = kImageColor.r;
    pixels[i * 4 + 1] = kImageColor.g;
    pixels[i * 4 + 2] = kImageColor.b;
    pixels[i * 4 + 3] = kImageColor.a;
  }

  // Write the pixels generated above to escher Image.
  escher::image_utils::WritePixelsToImage(uploader.get(), pixels.data(), image);
  uploader->Submit();
  escher_ptr->vk_device().waitIdle();

  // Export the escher image as vmo for GpuImage creation.
  zx::vmo image_vmo = escher::ExportMemoryAsVmo(escher_ptr.get(), gpu_mem_ptr);
  uint64_t vmo_size = 0;
  auto get_size_result = image_vmo.get_size(&vmo_size);
  ASSERT_TRUE(get_size_result == ZX_OK);

  // Create a GPU image using the vmo exported above.
  fuchsia::images::ImageInfo fx_image_info{
      .width = kImageSize,
      .height = kImageSize,
      .stride = static_cast<uint32_t>(
          kImageSize * images::StrideBytesPerWidthPixel(fuchsia::images::PixelFormat::R8G8B8A8)),
      .pixel_format = fuchsia::images::PixelFormat::R8G8B8A8,
      .tiling = fuchsia::images::Tiling::GPU_OPTIMAL,
  };

  // Present the external GPU image using BackgroundView.
  view.SetImage(std::move(image_vmo), vmo_size, fx_image_info,
                fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  RunUntilIndirectPresent(&view);

  scenic::Screenshot screenshot = TakeScreenshot();
  ASSERT_FALSE(screenshot.empty());

  std::map<scenic::Color, size_t> histogram = screenshot.Histogram();

  EXPECT_GT(histogram[kImageColor], 0u);
  histogram.erase(kImageColor);

  // This assert is written this way so that, when it fails, it prints out all
  // the unexpected colors
  EXPECT_EQ((std::map<scenic::Color, size_t>){}, histogram) << "Unexpected colors";
}

// Create the following Scene:
// ----------------------------------
// |            View 1              |
// |             red                |
// |--------------------------------|
// |    blue    View 2   green      |
// |              :                 |
// ----------------------------------
//
// This test case creates three Views: View 1 (containing one red ShapeNode),
// View 2 (containing one blue ShapeNode), Annotation View (containing one green
// ShapeNode).
//
// This test case uses fuchsia.ui.annotation.Registry FIDL API to create
// ViewHolder of Annotation View and attach Annotation View to scene later when
// we call Present() on any Session.
//
// View 2 and Annotation View should have the same View properties.
//
TEST_F(ScenicPixelTest, AnnotationTest) {
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;

  // Initialize second session
  auto unique_session_view1 = std::make_unique<scenic::Session>(scenic());
  auto unique_session_view2 = std::make_unique<scenic::Session>(scenic());
  auto unique_session_annotation = std::make_unique<scenic::Session>(scenic());

  auto session_view1 = unique_session_view1.get();
  auto session_view2 = unique_session_view2.get();
  auto session_annotation = unique_session_annotation.get();

  session_view1->set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Session terminated.";
    QuitLoop();
  });
  session_view2->set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Session terminated.";
    QuitLoop();
  });
  session_annotation->set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Annotation Session terminated.";
    QuitLoop();
  });

  test_session->SetUpCamera().SetProjection(0);
  scenic::EntityNode entity_node(session);
  entity_node.SetTranslation(0, 0, 0);
  test_session->scene.AddChild(entity_node);

  // Create two sets of view/view-holder token pairs.
  auto [view_token_1, view_holder_token_1] = scenic::ViewTokenPair::New();
  auto [view_token_2, view_holder_token_2] = scenic::ViewTokenPair::New();
  auto [view_control_ref_2, view_ref_2] = scenic::ViewRefPair::New();
  auto [view_token_annotation, view_holder_token_annotation] = scenic::ViewTokenPair::New();

  fuchsia::ui::views::ViewRef view_ref_2_create;
  view_ref_2.Clone(&view_ref_2_create);
  scenic::View view1(session_view1, std::move(view_token_1), "View 1");
  scenic::View view2(session_view2, std::move(view_token_2), std::move(view_control_ref_2),
                     std::move(view_ref_2_create), "View 2");
  scenic::View view_annotation(session_annotation, std::move(view_token_annotation),
                               "View Annotation");
  scenic::ViewHolder view_holder1(session, std::move(view_holder_token_1), "ViewHolder 1");
  scenic::ViewHolder view_holder2(session, std::move(view_holder_token_2), "ViewHolder 2");

  // Bounds of each view should be the size of a quarter of the display with
  // origin at 0,0 relative to its transform node.
  const std::array<float, 3> bmin = {0.f, 0.f, -2.f};
  const std::array<float, 3> bmax = {display_width, display_height / 2, 1.f};
  const std::array<float, 3> imin = {0, 0, 0};
  const std::array<float, 3> imax = {0, 0, 0};
  view_holder1.SetViewProperties(bmin, bmax, imin, imax);
  view_holder2.SetViewProperties(bmin, bmax, imin, imax);
  view_holder2.SetTranslation(0, display_height / 2, 0);

  // Pane extends across the entire right-side of the display, even though
  // its containing view is only in the top-right corner.
  int32_t pane_width = display_width;
  int32_t pane_height = display_height / 2;
  FX_LOGS(ERROR) << pane_width << " " << pane_height;
  scenic::Rectangle pane_shape(session_view1, pane_width, pane_height);
  scenic::Rectangle pane_shape2(session_view2, pane_width / 2, pane_height);
  scenic::Rectangle pane_shape_annotation(session_annotation, pane_width / 2, pane_height);

  // Create pane materials.
  scenic::Material pane_material_view1(session_view1);
  scenic::Material pane_material_view2(session_view2);
  scenic::Material pane_material_annotation(session_annotation);
  pane_material_view1.SetColor(255, 0, 0, 255);       // Red
  pane_material_view2.SetColor(0, 0, 255, 255);       // Blue
  pane_material_annotation.SetColor(0, 255, 0, 255);  // Green

  scenic::ShapeNode pane_node(session_view1);
  pane_node.SetShape(pane_shape);
  pane_node.SetMaterial(pane_material_view1);
  pane_node.SetTranslation(pane_width / 2, pane_height / 2, 0);

  scenic::ShapeNode pane_node2(session_view2);
  pane_node2.SetShape(pane_shape2);
  pane_node2.SetMaterial(pane_material_view2);
  pane_node2.SetTranslation(pane_width / 4, pane_height / 2, 0);

  scenic::ShapeNode pane_node_annotation(session_annotation);
  pane_node_annotation.SetShape(pane_shape_annotation);
  pane_node_annotation.SetMaterial(pane_material_annotation);
  pane_node_annotation.SetTranslation(pane_width * 3 / 4, pane_height / 2, 0);

  // Add view holders to the transform.
  entity_node.AddChild(view_holder1);
  view1.AddChild(pane_node);
  entity_node.AddChild(view_holder2);
  view2.AddChild(pane_node2);
  view_annotation.AddChild(pane_node_annotation);

  Present(session);
  Present(session_view1);
  Present(session_view2);
  Present(session_annotation);

  bool view_holder_annotation_created = false;
  fuchsia::ui::views::ViewRef view_ref_2_annotation;
  view_ref_2.Clone(&view_ref_2_annotation);
  annotation_registry()->CreateAnnotationViewHolder(
      std::move(view_ref_2_annotation), std::move(view_holder_token_annotation),
      [&view_holder_annotation_created]() { view_holder_annotation_created = true; });

  RunLoopWithTimeout(zx::msec(100));
  EXPECT_FALSE(view_holder_annotation_created);

  {
    scenic::Screenshot screenshot = TakeScreenshot();
    scenic::Color red_color = screenshot.ColorAt(0.5, 0.25);
    scenic::Color blue_color = screenshot.ColorAt(0.25, 0.75);
    scenic::Color black_color = screenshot.ColorAt(0.75, 0.75);
    EXPECT_EQ(red_color, scenic::Color(255, 0, 0, 255));
    EXPECT_EQ(blue_color, scenic::Color(0, 0, 255, 255));
    EXPECT_EQ(black_color, scenic::Color(0, 0, 0, 0));
  }

  Present(session_view2);
  EXPECT_TRUE(view_holder_annotation_created);

  {
    scenic::Screenshot screenshot = TakeScreenshot();
    scenic::Color red_color = screenshot.ColorAt(0.5, 0.25);
    scenic::Color blue_color = screenshot.ColorAt(0.25, 0.75);
    scenic::Color green_color = screenshot.ColorAt(0.75, 0.75);
    EXPECT_EQ(red_color, scenic::Color(255, 0, 0, 255));
    EXPECT_EQ(blue_color, scenic::Color(0, 0, 255, 255));
    EXPECT_EQ(green_color, scenic::Color(0, 255, 0, 255));
  }
}

}  // namespace
