// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/lab/pose_buffer_presenter/app.h"

// This header is intentionally out of order because it contains a workaround
// for both glm and zircon defining countof(), and must be included before
// the glm headers to work.
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/clock.h>

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>
#include <iostream>

#include "lib/ui/scenic/cpp/commands.h"
#include "src/lib/ui/scenic/cpp/host_memory.h"
#include "src/lib/ui/scenic/cpp/mesh_utils.h"
#include <lib/syslog/cpp/macros.h>
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/hmd/pose_buffer.h"
#include "src/ui/lib/escher/util/image_utils.h"

#define DEBUG_BOX 0

using namespace scenic;

namespace pose_buffer_presenter {

namespace {

constexpr float kSecondsPerNanosecond = .000'000'001f;

static constexpr float kEdgeLength = 0.125f;

static const float kVertexBufferData[] = {
    -1.0f, -1.0f, -1.0f,  // 0
    -1.0f, -1.0f, 1.0f,   // 1
    -1.0f, 1.0f,  -1.0f,  // 2
    -1.0f, 1.0f,  1.0f,   // 3
    1.0f,  -1.0f, -1.0f,  // 4
    1.0f,  -1.0f, 1.0f,   // 5
    1.0f,  1.0f,  -1.0f,  // 6
    1.0f,  1.0f,  1.0f,   // 7
};

static const uint32_t kIndexBufferData[] = {
    5, 6, 7, 6, 5, 4,  // +X
    0, 1, 2, 3, 2, 1,  // -X
    2, 3, 6, 7, 6, 3,  // +Y
    1, 4, 5, 4, 1, 0,  // -Y
    3, 5, 7, 5, 3, 1,  // +Z
    0, 2, 4, 6, 4, 2,  // -Z
};
}  // namespace

App::App(async::Loop* loop)
    : component_context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()), loop_(loop) {
  // Connect to the Scenic service.
  scenic_ = component_context_->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(INFO) << "Lost connection to Scenic service. Status: " << status;
    loop_->Quit();
  });
  scenic_->GetDisplayInfo(
      [this](fuchsia::ui::gfx::DisplayInfo display_info) { Init(std::move(display_info)); });
}

void App::CreateExampleScene(float display_width, float display_height) {
  auto session = session_.get();

  // The top-level nesting for drawing anything is compositor -> layer-stack
  // -> layer.  Layer content can come from an image, or by rendering a scene.
  // In this case, we do the latter, so we nest layer -> renderer -> camera ->
  // scene.
  compositor_ = std::make_unique<DisplayCompositor>(session);
  LayerStack layer_stack(session);
  Layer layer(session);
  Renderer renderer(session);
  Scene scene(session);
  camera_ = std::make_unique<StereoCamera>(scene);

  // Produces the Identity View Matrix
  static const glm::vec3 eye(0, 0, 0);
  static const glm::vec3 look_at(0, 0, -1);
  static const glm::vec3 up(0, 1, 0);
  camera_->SetTransform({eye.x, eye.y, eye.z}, {look_at.x, look_at.y, look_at.z},
                        {up.x, up.y, up.z});

  float fovy = glm::radians(30.f);
  float f = 1.0f / tan(0.5f * fovy);
  float aspect_ratio = (display_width * 0.5f) / display_height;
  float near = 0.1;
  float far = 10;
  // Use (display_width * 0.5f) / display_height because the stereo camera uses
  // half of the display for each eye, so the aspect ratio for each eye has 1/2
  // the width:height ratio of the display.
  // clang-format off
  glm::mat4 projection( f / aspect_ratio,  0.0f, 0.0f,                        0.0f,
                        0.0f,                -f, 0.0f,                        0.0f,
                        0.0f,              0.0f, far / (near - far),         -1.0f,
                        0.0f,              0.0f, (near * far) / (near - far), 0.0f);
  // clang-format on
  std::array<float, 16> projection_arr;
  const float* projection_ptr = glm::value_ptr(projection);
  std::copy(projection_ptr, projection_ptr + 16, std::begin(projection_arr));
  camera_->SetStereoProjection(projection_arr, projection_arr);

  compositor_->SetLayerStack(layer_stack);
  layer_stack.AddLayer(layer);
  layer.SetSize(display_width, display_height);
  layer.SetRenderer(renderer);
  renderer.SetCamera(camera_->id());
  renderer.SetShadowTechnique(fuchsia::ui::gfx::ShadowTechnique::UNSHADOWED);

  // Set up lights.
  AmbientLight ambient_light(session);
  DirectionalLight directional_light(session);
  scene.AddLight(ambient_light);
  scene.AddLight(directional_light);
  ambient_light.SetColor(0.3f, 0.3f, 0.3f);
  directional_light.SetColor(0.7f, 0.7f, 0.7f);
  directional_light.SetDirection(1.f, 1.f, -2.f);

  // Create an EntityNode to serve as the scene root.
  EntityNode root_node(session);
  scene.AddChild(root_node.id());

  cube_node_ = std::make_unique<ShapeNode>(session);
  Material cube_material(session);
  cube_material.SetColor(0xf5, 0x00, 0x57, 0xff);  // Pink A400
  cube_node_->SetMaterial(cube_material);

  std::vector<float> vertices(std::begin(kVertexBufferData), std::end(kVertexBufferData));
  std::vector<uint32_t> indices(std::begin(kIndexBufferData), std::end(kIndexBufferData));
  auto cube_shape = scenic_util::NewMeshWithVertices(session, vertices, indices);

  cube_node_->SetShape(*cube_shape);
  // Raw vertex data has an edge length of 2, so we must scale by half of
  // kEdgeLength to end up with a cube whose edge length is kEdgeLength long.
  float scale_factor = 0.5 * kEdgeLength;
  cube_node_->SetScale(scale_factor, scale_factor, scale_factor);
  cube_node_->SetTranslation(0, 4.0 * kEdgeLength, 0);

  root_node.AddChild(*cube_node_);

// Adds a colored box arround the camera to help debug orientation problems
#if DEBUG_BOX
  float pane_width = 10;
  scenic::Rectangle pane_shape(session, pane_width, pane_width);

  static const int num_panes = 6;

  glm::vec4 colors[num_panes] = {
      glm::vec4(255, 0, 0, 255),    // RED
      glm::vec4(0, 255, 255, 255),  // CYAN
      glm::vec4(0, 255, 0, 255),    // GREEN
      glm::vec4(255, 0, 255, 255),  // MAGENTA
      glm::vec4(0, 0, 255, 255),    // BLUE
      glm::vec4(255, 255, 0, 255),  // YELLOW
  };

  static const float pane_offset = pane_width / 2;

  glm::vec3 translations[num_panes] = {
      glm::vec3(0, 0, pane_offset),   // Above Camera
      glm::vec3(0, 0, -pane_offset),  // Below Camera
      glm::vec3(pane_offset, 0, 0),   // Right of camera
      glm::vec3(-pane_offset, 0, 0),  // Left of Camera
      glm::vec3(0, pane_offset, 0),   // In front of camera.
      glm::vec3(0, -pane_offset, 0),  // Behind camera.
  };

  float pi = glm::pi<float>();
  glm::quat orientations[num_panes] = {
      glm::quat(),  // identity quaternion
      glm::angleAxis(pi, glm::vec3(1, 0, 0)),
      glm::angleAxis(pi / 2, glm::vec3(0, 1, 0)),
      glm::angleAxis(-pi / 2, glm::vec3(0, 1, 0)),
      glm::angleAxis(-pi / 2, glm::vec3(1, 0, 0)),
      glm::angleAxis(pi / 2, glm::vec3(1, 0, 0)),

  };

  for (int i = 0; i < num_panes; i++) {
    glm::vec4 color = colors[i];
    glm::vec3 translation = translations[i];
    glm::quat orientation = orientations[i];

    scenic::Material pane_material(session);
    pane_material.SetColor(color.r, color.g, color.b, color.a);
    scenic::ShapeNode pane_shape_node(session);
    pane_shape_node.SetShape(pane_shape);
    pane_shape_node.SetMaterial(pane_material);
    pane_shape_node.SetTranslation(translation.x, translation.y, translation.z);
    pane_shape_node.SetRotation(orientation.x, orientation.y, orientation.z, orientation.w);
    root_node.AddChild(pane_shape_node);
  }
#endif
}

void App::StartPoseBufferProvider() {
  FX_LOGS(INFO) << "Launching PoseBufferProvider";

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url =
      "fuchsia-pkg://fuchsia.com/pose_buffer_provider#meta/"
      "pose_buffer_provider.cmx";
  auto services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  fuchsia::sys::LauncherSyncPtr launcher;
  component_context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  controller_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Lost connection to controller_. Status: " << status;
  });

  services->Connect(provider_.NewRequest());
  provider_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Lost connection to PoseBufferProvider service. Status: " << status;
  });
}

void App::ConfigurePoseBuffer() {
  auto session = session_.get();

  uint64_t vmo_size = PAGE_SIZE;
  zx::vmo vmo;
  zx_status_t status;
  status = zx::vmo::create(vmo_size, 0u, &pose_buffer_vmo_);
  FX_DCHECK(status == ZX_OK);
  status = pose_buffer_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FX_DCHECK(status == ZX_OK);

  zx_time_t base_time = zx::clock::get_monotonic().get();
  // Normally the time interval is the period of time between each entry in the
  // pose buffer. In this example we only use one entry so the time interval is
  // pretty meaningless. Set to 1 for simplicity (see fxbug.dev/327).
  zx_time_t time_interval = 1;
  uint32_t num_entries = 1;

  Memory mem(session, std::move(vmo), vmo_size, fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  Buffer pose_buffer(mem, 0, vmo_size);

  camera_->SetPoseBuffer(pose_buffer, num_entries, base_time, time_interval);

  status = pose_buffer_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FX_DCHECK(status == ZX_OK);

  provider_->SetPoseBuffer(std::move(vmo), num_entries, base_time, time_interval);
}

void App::Init(fuchsia::ui::gfx::DisplayInfo display_info) {
  StartPoseBufferProvider();
  FX_LOGS(INFO) << "Creating new Session";

  // TODO: set up SessionListener.
  session_ = std::make_unique<Session>(scenic_.get());
  session_->set_error_handler([this](zx_status_t status) {
    FX_LOGS(INFO) << "Session terminated. Status: " << status;
    loop_->Quit();
  });

  // Set up initial scene.
  const float display_width = static_cast<float>(display_info.width_in_px);
  const float display_height = static_cast<float>(display_info.height_in_px);
  CreateExampleScene(display_width, display_height);
  ConfigurePoseBuffer();

  start_time_ = zx_clock_get_monotonic();
  Update(start_time_);
}

void App::Update(uint64_t next_presentation_time) {
  float secs = zx_clock_get_monotonic() * kSecondsPerNanosecond;

  glm::quat quaternion = glm::angleAxis(secs / 2.0f, glm::normalize(glm::vec3(0, 1, 0)));

  cube_node_->SetRotation(quaternion.x, quaternion.y, quaternion.z, quaternion.w);

  // Present
  session_->Present(next_presentation_time, [this](fuchsia::images::PresentationInfo info) {
    Update(info.presentation_time + info.presentation_interval);
  });
}

void App::ReleaseSessionResources() {
  FX_LOGS(INFO) << "Closing session.";

  compositor_.reset();
  camera_.reset();

  session_.reset();
}

}  // namespace pose_buffer_presenter
