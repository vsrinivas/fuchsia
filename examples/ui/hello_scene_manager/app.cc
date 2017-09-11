// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/hello_scene_manager/app.h"

#if defined(countof)
// Workaround for compiler error due to Magenta defining countof() as a macro.
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

#include "lib/app/cpp/connect.h"
#include "escher/util/image_utils.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

#include "lib/ui/scenic/client/host_memory.h"
#include "lib/ui/scenic/fidl_helpers.h"
#include "lib/ui/scenic/types.h"
#include "lib/ui/scenic/fidl/ops.fidl.h"
#include "lib/ui/scenic/fidl/scene_manager.fidl.h"
#include "lib/ui/scenic/fidl/session.fidl.h"
#include "garnet/bin/ui/scene_manager/tests/util.h"

using namespace mozart;
using namespace scenic_lib;

namespace hello_scene_manager {

static constexpr uint64_t kBillion = 1000000000;

App::App()
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      loop_(mtl::MessageLoop::GetCurrent()) {
  // Connect to the SceneManager service.
  scene_manager_ =
      application_context_->ConnectToEnvironmentService<scenic::SceneManager>();
  scene_manager_.set_connection_error_handler([this] {
    FXL_LOG(INFO) << "Lost connection to SceneManager service.";
    loop_->QuitNow();
  });
  scene_manager_->GetDisplayInfo([this](scenic::DisplayInfoPtr display_info) {
    Init(std::move(display_info));
  });
}

void App::InitCheckerboardMaterial(Material* uninitialized_material) {
  // Generate a checkerboard material.  This is a multi-step process:
  //   - generate pixels for the material.
  //   - create a VMO that contains these pixels.
  //   - duplicate the VMO handle and use it to create a Session Memory obj.
  //   - use the Memory obj to create an Image obj.
  //   - use the Image obj as a Material's texture.
  size_t checkerboard_width = 8;
  size_t checkerboard_height = 8;
  size_t checkerboard_pixels_size;
  auto checkerboard_pixels = escher::image_utils::NewGradientPixels(
      checkerboard_width, checkerboard_height, &checkerboard_pixels_size);

  HostMemory checkerboard_memory(session_.get(), checkerboard_pixels_size);
  memcpy(checkerboard_memory.data_ptr(), checkerboard_pixels.get(),
         checkerboard_pixels_size);

  // Create an Image to wrap the checkerboard.
  auto checkerboard_image_info = scenic::ImageInfo::New();
  checkerboard_image_info->width = checkerboard_width;
  checkerboard_image_info->height = checkerboard_height;
  const size_t kBytesPerPixel = 4u;
  checkerboard_image_info->stride = checkerboard_width * kBytesPerPixel;
  checkerboard_image_info->pixel_format =
      scenic::ImageInfo::PixelFormat::BGRA_8;
  checkerboard_image_info->color_space = scenic::ImageInfo::ColorSpace::SRGB;
  checkerboard_image_info->tiling = scenic::ImageInfo::Tiling::LINEAR;

  HostImage checkerboard_image(checkerboard_memory, 0,
                               std::move(checkerboard_image_info));

  uninitialized_material->SetTexture(checkerboard_image.id());
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

  compositor_->SetLayerStack(layer_stack);
  layer_stack.AddLayer(layer);
  layer.SetSize(display_width, display_height);
  layer.SetRenderer(renderer);
  renderer.SetCamera(camera_->id());

  // Create an EntityNode to serve as the scene root.
  EntityNode root_node(session);
  scene.AddChild(root_node.id());

  static constexpr float kPaneMargin = 100.f;
  static const float pane_width = (display_width - 3 * kPaneMargin) / 2.f;
  static const float pane_height = display_height - 2 * kPaneMargin;

  // The root node will enclose two "panes", each with a rounded-rect part
  // that acts as a background clipper.
  RoundedRectangle pane_shape(session, pane_width, pane_height, 20, 20, 80, 10);
  Material pane_material(session);
  pane_material.SetColor(120, 120, 255, 255);

  EntityNode pane_node_1(session);
  ShapeNode pane_bg_1(session);
  pane_bg_1.SetShape(pane_shape);
  pane_bg_1.SetMaterial(pane_material);
  pane_node_1.AddPart(pane_bg_1);
  pane_node_1.SetTranslation(kPaneMargin + pane_width * 0.5,
                             kPaneMargin + pane_height * 0.5, 20);
  pane_node_1.SetClip(0, true);
  root_node.AddChild(pane_node_1);

  EntityNode pane_node_2(session);
  ShapeNode pane_bg_2(session);
  pane_bg_2.SetShape(pane_shape);
  pane_bg_2.SetMaterial(pane_material);
  pane_node_2.AddPart(pane_bg_2);
  pane_node_2.SetTranslation(kPaneMargin * 2 + pane_width * 1.5,
                             kPaneMargin + pane_height * 0.5, 20);
  pane_node_2.SetClip(0, true);
  root_node.AddChild(pane_node_2);

  // Create a Material with the checkerboard image.  This will be used for
  // the objects in each pane.
  Material checkerboard_material(session);
  InitCheckerboardMaterial(&checkerboard_material);
  checkerboard_material.SetColor(255, 100, 100, 255);

  Material green_material(session);
  green_material.SetColor(50, 150, 50, 255);

  // The first pane will contain an animated rounded-rect.
  rrect_node_ = std::make_unique<ShapeNode>(session);
  rrect_node_->SetMaterial(checkerboard_material);
  rrect_node_->SetShape(RoundedRectangle(session, 200, 300, 20, 20, 80, 10));
  pane_node_1.AddChild(rrect_node_->id());

  // The second pane will contain two large circles that are clipped by a pair
  // of smaller animated circles.
  EntityNode pane_2_contents(session);

  Circle clipper_circle(session, 200);
  clipper_1_ = std::make_unique<ShapeNode>(session);
  clipper_2_ = std::make_unique<ShapeNode>(session);
  clipper_1_->SetShape(clipper_circle);
  clipper_2_->SetShape(clipper_circle);

  Circle clippee_circle(session, 400);
  ShapeNode clippee1(session);
  clippee1.SetShape(clippee_circle);
  clippee1.SetMaterial(green_material);
  clippee1.SetTranslation(0, 400, 0);
  ShapeNode clippee2(session);
  clippee2.SetShape(clippee_circle);
  clippee2.SetMaterial(checkerboard_material);
  clippee2.SetTranslation(0, -400, 0);

  pane_2_contents.AddPart(clipper_1_->id());
  pane_2_contents.AddPart(clipper_2_->id());
  pane_2_contents.AddChild(clippee1);
  pane_2_contents.AddChild(clippee2);
  pane_2_contents.SetClip(0, true);

  pane_node_2.AddChild(pane_2_contents);
  pane_2_contents.SetTranslation(0, 0, 10);
}

void App::Init(scenic::DisplayInfoPtr display_info) {
  FXL_LOG(INFO) << "Creating new Session";

  // TODO: set up SessionListener.
  session_ = std::make_unique<scenic_lib::Session>(scene_manager_.get());
  session_->set_connection_error_handler([this] {
    FXL_LOG(INFO) << "Session terminated.";
    loop_->QuitNow();
  });

  // Wait kSessionDuration seconds, and close the session.
  constexpr int kSessionDuration = 40;
  loop_->task_runner()->PostDelayedTask(
      [this] { ReleaseSessionResources(); },
      fxl::TimeDelta::FromSeconds(kSessionDuration));

  // Set up initial scene.
  const float display_width = static_cast<float>(display_info->physical_width);
  const float display_height =
      static_cast<float>(display_info->physical_height);
  CreateExampleScene(display_width, display_height);

  start_time_ = mx_time_get(MX_CLOCK_MONOTONIC);
  camera_anim_start_time_ = start_time_;
  Update(start_time_);
}

void App::Update(uint64_t next_presentation_time) {
  // Translate / rotate the rounded rect.
  {
    double secs =
        static_cast<double>(next_presentation_time - start_time_) / kBillion;

    rrect_node_->SetTranslation(sin(secs * 0.8) * 500.f,
                                sin(secs * 0.6) * 570.f, 10.f);

    auto quaternion =
        glm::angleAxis(static_cast<float>(secs / 2.0), glm::vec3(0, 0, 1));
    rrect_node_->SetRotation(quaternion.x, quaternion.y, quaternion.z,
                             quaternion.w);
  }

  // Translate the clip-circles.
  {
    double secs =
        static_cast<double>(next_presentation_time - start_time_) / kBillion;

    float offset1 = sin(secs * 0.8) * 300.f;
    float offset2 = cos(secs * 0.8) * 300.f;

    clipper_1_->SetTranslation(offset1, offset2 * 3, -5);
    clipper_2_->SetTranslation(offset2, offset1 * 2, -4);
  }

  // Move the camera.
  {
    double secs =
        static_cast<double>(next_presentation_time - camera_anim_start_time_) /
        kBillion;
    const double kCameraModeDuration = 5.0;
    float param = secs / kCameraModeDuration;
    if (param > 1.0) {
      param = 0.0;
      camera_anim_returning_ = !camera_anim_returning_;
      camera_anim_start_time_ = next_presentation_time;
    }
    if (camera_anim_returning_) {
      param = 1.0 - param;
    }

    // Animate the eye position.
    glm::vec3 eye_start(1080, 720, 6000);
    glm::vec3 eye_end(0, 10000, 7000);
    glm::vec3 eye =
        glm::mix(eye_start, eye_end, glm::smoothstep(0.f, 1.f, param));

    // Always look at the middle of the stage.
    float target[3] = {1080, 720, 0};
    float up[3] = {0, 1, 0};

    camera_->SetProjection(glm::value_ptr(eye), target, up, glm::radians(15.f));
  }

  // Present
  session_->Present(
      next_presentation_time, [this](scenic::PresentationInfoPtr info) {
        Update(info->presentation_time + info->presentation_interval);
      });
}

void App::ReleaseSessionResources() {
  FXL_LOG(INFO) << "Closing session.";

  compositor_.reset();
  camera_.reset();
  clipper_2_.reset();
  clipper_1_.reset();
  rrect_node_.reset();

  session_.reset();
}

}  // namespace hello_scene_manager
