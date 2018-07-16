// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/hello_pose_buffer/app.h"

#if defined(countof)
// Workaround for compiler error due to Zircon defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "lib/component/cpp/connect.h"
#include "lib/escher/util/image_utils.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

#include "lib/escher/hmd/pose_buffer.h"
#include "lib/ui/scenic/cpp/host_memory.h"
#include "lib/ui/scenic/fidl_helpers.h"
#include "lib/ui/scenic/types.h"

using namespace scenic;

namespace hello_pose_buffer {

static constexpr float kEdgeLength = 900;
;

static constexpr uint64_t kBillion = 1000000000;

App::App(async::Loop* loop)
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      loop_(loop) {
  // Connect to the Mozart service.
  scenic_ = startup_context_
                ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([this] {
    FXL_LOG(INFO) << "Lost connection to Mozart service.";
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
  camera_ = std::make_unique<Camera>(scene);

  float camera_offset = kEdgeLength * 4.f;
  float eye_position[3] = {0, 0, camera_offset};
  float look_at[3] = {0, 0, 0};
  float up[3] = {0, 1, 0};
  float fovy = glm::radians(30.f);

  camera_->SetTransform(eye_position, look_at, up);
  camera_->SetProjection(fovy);

  compositor_->SetLayerStack(layer_stack);
  layer_stack.AddLayer(layer);
  layer.SetSize(display_width, display_height);
  layer.SetRenderer(renderer);
  renderer.SetCamera(camera_->id());

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

  static const int num_checkers = 3;
  static const float checker_length = kEdgeLength / num_checkers;
  Rectangle checker_shape(session, kEdgeLength / num_checkers,
                          kEdgeLength / num_checkers);

  Material light_material(session);
  light_material.SetColor(120, 120, 120, 255);

  Material dark_material(session);
  dark_material.SetColor(20, 20, 20, 255);

  Material materials[2] = {std::move(light_material), std::move(dark_material)};

  for (int i = 0; i < num_checkers; i++) {
    for (int j = 0; j < num_checkers; j++) {
      int material_index = (i + (j % 2)) % 2;
      glm::vec3 translation(
          kEdgeLength * -0.5 + checker_length * i + checker_length / 2,
          kEdgeLength * -0.5 + checker_length * j + checker_length / 2,
          kEdgeLength);

      // EntityNode checker_node(session);
      ShapeNode checker_shape_node(session);
      checker_shape_node.SetShape(checker_shape);
      checker_shape_node.SetMaterial(materials[material_index]);
      // checker_node.AddPart(checker_shape_node);
      checker_shape_node.SetTranslation(translation.x, translation.y,
                                        translation.z);
      root_node.AddChild(checker_shape_node);
    }
  }

  uint64_t vmo_size = PAGE_SIZE;
  zx::vmo vmo;
  zx_status_t status;
  status = zx::vmo::create(vmo_size, 0u, &pose_buffer_vmo_);
  FXL_DCHECK(status == ZX_OK);
  status = pose_buffer_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FXL_DCHECK(status == ZX_OK);

  uint64_t base_time = zx::clock::get_monotonic().get();
  uint64_t time_interval = 1024 * 1024 * 60 / 3.0;  // 16.67 ms
  uint32_t num_entries = 1;

  Memory mem(session, std::move(vmo),
             fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  Buffer pose_buffer(mem, 0, vmo_size);

  camera_->SetPoseBuffer(pose_buffer, num_entries, base_time, time_interval);
}

void App::Init(fuchsia::ui::gfx::DisplayInfo display_info) {
  FXL_LOG(INFO) << "Creating new Session";

  // TODO: set up SessionListener.
  session_ = std::make_unique<scenic::Session>(scenic_.get());
  session_->set_error_handler([this] {
    FXL_LOG(INFO) << "Session terminated.";
    loop_->Quit();
  });

  // Wait kSessionDuration seconds, and close the session.
  constexpr zx::duration kSessionDuration = zx::sec(40);
  async::PostDelayedTask(loop_->dispatcher(), [this] { ReleaseSessionResources(); },
                         kSessionDuration);

  // Set up initial scene.
  const float display_width = static_cast<float>(display_info.width_in_px);
  const float display_height = static_cast<float>(display_info.height_in_px);
  CreateExampleScene(display_width, display_height);

  start_time_ = zx_clock_get(ZX_CLOCK_MONOTONIC);
  Update(start_time_);
}

void App::Update(uint64_t next_presentation_time) {
  double secs =
      static_cast<double>(next_presentation_time - start_time_) / kBillion;

  glm::vec3 pos(0, kEdgeLength / 2, 0);
  glm::quat quat =
      glm::angleAxis(static_cast<float>(secs / 2.0), glm::vec3(0, 0, 1));
  escher::hmd::Pose pose(quat, pos);

  // Use vmo::write here for test simplicity. In a real case the vmo should be
  // mapped into a vmar so we dont need a syscall per write
  zx_status_t status =
      pose_buffer_vmo_.write(&pose, 0, sizeof(escher::hmd::Pose));
  FXL_DCHECK(status == ZX_OK);

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

}  // namespace hello_pose_buffer
