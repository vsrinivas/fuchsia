// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/hello_stereo/app.h"

#if defined(countof)
// Workaround for compiler error due to Zircon defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway. (SCN-666)
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "lib/component/cpp/connect.h"
#include "lib/escher/util/image_utils.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/ui/scenic/cpp/host_memory.h"
#include "lib/ui/scenic/cpp/commands.h"

using namespace scenic;

namespace hello_stereo {

static constexpr float kEdgeLength = 900;

App::App(async::Loop* loop)
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      loop_(loop) {
  // Connect to the SceneManager service.
  scenic_ = startup_context_
                ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([this] {
    FXL_LOG(INFO) << "Lost connection to Scenic service.";
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
  auto camera = std::make_unique<StereoCamera>(scene);

  float camera_offset = kEdgeLength * 4.f;
  float eye_position[3] = {0, 0, camera_offset};
  float look_at[3] = {0, 0, 0};
  float up[3] = {0, 1, 0};

  camera->SetTransform(eye_position, look_at, up);

  float fovy = glm::radians(30.f);
  glm::mat4 projection = glm::perspective(
      fovy, (display_width * 0.5f) / display_height, 0.1f, camera_offset);
  camera->SetStereoProjection(glm::value_ptr(projection),
                              glm::value_ptr(projection));

  compositor_->SetLayerStack(layer_stack);
  layer_stack.AddLayer(layer);
  layer.SetSize(display_width, display_height);
  layer.SetRenderer(renderer);
  renderer.SetCamera(camera->id());

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
  // Present
  session_->Present(
      next_presentation_time, [this](fuchsia::images::PresentationInfo info) {
        Update(info.presentation_time + info.presentation_interval);
      });
}

void App::ReleaseSessionResources() {
  FXL_LOG(INFO) << "Closing session.";

  compositor_.reset();

  session_.reset();
}

}  // namespace hello_stereo
