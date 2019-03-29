// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/lab/pose_buffer_provider/app.h"

// This header is intentionally out of order because it contains a workaround
// for both glm and zircon defining countof(), and must be included before
// the glm headers to work.
#include "lib/escher/geometry/types.h"

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include <iostream>

#include "lib/component/cpp/connect.h"
#include "lib/escher/hmd/pose_buffer.h"
#include "lib/escher/util/image_utils.h"
#include "src/lib/fxl/logging.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "lib/ui/scenic/cpp/host_memory.h"
#include "lib/ui/scenic/cpp/util/mesh_utils.h"

using namespace scenic;

namespace pose_buffer_provider {

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
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      loop_(loop) {
  // Connect to the Scenic service.
  scenic_ = startup_context_
                ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(INFO) << "Lost connection to Scenic service. Status: " << status;
    loop_->Quit();
  });
  scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
    Init(std::move(display_info));
  });
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

  float eye_position[3] = {0, 0, 0};
  float look_at[3] = {0, -1, 0};
  float up[3] = {0, 0, 1};

  camera_->SetTransform(eye_position, look_at, up);

  float fovy = glm::radians(30.f);
  // Use (display_width * 0.5f) / display_height because the stereo camera uses
  // half of the display for each eye, so the aspect ratio for each eye has 1/2
  // the width:height ratio of the display.
  glm::mat4 projection =
      glm::perspective(fovy, (display_width * 0.5f) / display_height,
                       kEdgeLength / 100.f, kEdgeLength * 8);
  camera_->SetStereoProjection(glm::value_ptr(projection),
                               glm::value_ptr(projection));

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

  std::vector<float> vertices(std::begin(kVertexBufferData),
                              std::end(kVertexBufferData));
  std::vector<uint32_t> indices(std::begin(kIndexBufferData),
                                std::end(kIndexBufferData));
  auto cube_shape = mesh_utils::NewMeshWithVertices(session, vertices, indices);

  cube_node_->SetShape(*cube_shape);
  // Raw vertex data has an edge length of 2, so we must scale by half of
  // kEdgeLength to end up with a cube whose edge length is kEdgeLength long.
  float scale_factor = 0.5 * kEdgeLength;
  cube_node_->SetScale(scale_factor, scale_factor, scale_factor);
  cube_node_->SetTranslation(0, 0, -4.0 * kEdgeLength);

  root_node.AddChild(*cube_node_);
}

void App::ConfigurePoseBuffer() {
  auto session = session_.get();

  uint64_t vmo_size = PAGE_SIZE;
  zx::vmo vmo;
  zx_status_t status;
  status = zx::vmo::create(vmo_size, 0u, &pose_buffer_vmo_);
  FXL_DCHECK(status == ZX_OK);
  status = pose_buffer_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FXL_DCHECK(status == ZX_OK);

  zx_time_t base_time = zx::clock::get_monotonic().get();
  // Normally the time interval is the period of time between each entry in the
  // pose buffer. In this example we only use one entry so the time interval is
  // pretty meaningless. Set to 1 for simplicity (see ARGO-21).
  zx_time_t time_interval = 1;
  uint32_t num_entries = 1;

  Memory mem(session, std::move(vmo), vmo_size,
             fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  Buffer pose_buffer(mem, 0, vmo_size);

  camera_->SetPoseBuffer(pose_buffer, num_entries, base_time, time_interval);

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url =
      "fuchsia-pkg://fuchsia.com/pose_buffer_provider#meta/"
      "pose_buffer_provider.cmx";
  launch_info.directory_request = services_.NewRequest();
  startup_context_->launcher()->CreateComponent(std::move(launch_info),
                                                controller_.NewRequest());
  controller_.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "Lost connection to controller_. Status: " << status;
  });

  services_.ConnectToService(provider_.NewRequest().TakeChannel(),
                             fuchsia::ui::gfx::PoseBufferProvider::Name_);

  provider_.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "Lost connection to PoseBufferProvider service. Status: "
                   << status;
  });

  status = pose_buffer_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FXL_DCHECK(status == ZX_OK);

  provider_->SetPoseBuffer(std::move(vmo), num_entries, base_time,
                           time_interval);
}

void App::Init(fuchsia::ui::gfx::DisplayInfo display_info) {
  FXL_LOG(INFO) << "Creating new Session";

  // TODO: set up SessionListener.
  session_ = std::make_unique<Session>(scenic_.get());
  session_->set_error_handler([this](zx_status_t status) {
    FXL_LOG(INFO) << "Session terminated. Status: " << status;
    loop_->Quit();
  });

  // Set up initial scene.
  const float display_width = static_cast<float>(display_info.width_in_px);
  const float display_height = static_cast<float>(display_info.height_in_px);
  CreateExampleScene(display_width, display_height);
  ConfigurePoseBuffer();

  start_time_ = zx_clock_get(ZX_CLOCK_MONOTONIC);
  Update(start_time_);
}

void App::Update(uint64_t next_presentation_time) {
  float secs = zx_clock_get(ZX_CLOCK_MONOTONIC) * kSecondsPerNanosecond;

  glm::quat quaternion =
      glm::angleAxis(secs / 2.0f, glm::normalize(glm::vec3(0, 1, 0)));

  cube_node_->SetRotation(quaternion.x, quaternion.y, quaternion.z,
                          quaternion.w);

  // Present
  session_->Present(
      next_presentation_time, [this](fuchsia::images::PresentationInfo info) {
        Update(info.presentation_time + info.presentation_interval);
      });
}

void App::ReleaseSessionResources() {
  FXL_LOG(INFO) << "Closing session.";

  compositor_.reset();
  camera_.reset();

  session_.reset();
}

}  // namespace pose_buffer_provider
