// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/eventpair.h>

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

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "escher/util/image_utils.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

#include "apps/mozart/lib/scene/client/resources.h"
#include "apps/mozart/lib/scene/client/session.h"
#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/lib/scene/types.h"
#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "apps/mozart/services/scene/ops.fidl.h"
#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "apps/mozart/services/scene/session.fidl.h"
#include "apps/mozart/src/scene/tests/util.h"

using namespace mozart;
using namespace mozart::client;

class HelloSceneManagerApp {
 public:
  HelloSceneManagerApp()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        loop_(mtl::MessageLoop::GetCurrent()) {
    // Connect to the SceneManager service.
    scene_manager_ = application_context_
                         ->ConnectToEnvironmentService<mozart2::SceneManager>();
    scene_manager_.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Lost connection to SceneManager service.";
      loop_->QuitNow();
    });
  }

  void CreateExampleScene() {
    auto session = session_.get();

    // Create an EntityNode to serve as the scene root.
    EntityNode entity_node(session);

    // Create two shape nodes, one for a circle and one for a rounded-rect.
    // Remember the rounded-rect node, because we're going to animate it.
    ShapeNode circle_node(session);
    rrect_node_ = std::make_unique<ShapeNode>(session);

    // Immediately attach them to the root.
    entity_node.AddChild(circle_node.id());
    entity_node.AddChild(rrect_node_->id());

    // Generate a checkerboard.
    size_t checkerboard_width = 8;
    size_t checkerboard_height = 8;
    size_t checkerboard_pixels_size;
    auto checkerboard_pixels = escher::image_utils::NewGradientPixels(
        checkerboard_width, checkerboard_height, &checkerboard_pixels_size);

    auto shared_vmo =
        mozart::scene::test::CreateSharedVmo(checkerboard_pixels_size);

    memcpy(shared_vmo->Map(), checkerboard_pixels.get(),
           checkerboard_pixels_size);

    // Duplicate the VMO handle.
    mx::vmo vmo_copy;
    auto status = shared_vmo->vmo().duplicate(MX_RIGHT_SAME_RIGHTS, &vmo_copy);
    if (status) {
      FTL_LOG(ERROR) << "Failed to duplicate vmo handle.";
      return;
    }

    Memory checkerboard_memory(session, std::move(vmo_copy),
                               mozart2::MemoryType::HOST_MEMORY);

    // Create an Image to wrap the checkerboard.
    auto checkerboard_image_info = mozart2::ImageInfo::New();
    checkerboard_image_info->width = checkerboard_width;
    checkerboard_image_info->height = checkerboard_height;
    const size_t kBytesPerPixel = 4u;
    checkerboard_image_info->stride = checkerboard_width * kBytesPerPixel;
    checkerboard_image_info->pixel_format =
        mozart2::ImageInfo::PixelFormat::BGRA_8;
    checkerboard_image_info->color_space = mozart2::ImageInfo::ColorSpace::SRGB;
    checkerboard_image_info->tiling = mozart2::ImageInfo::Tiling::LINEAR;

    Image checkerboard_image(checkerboard_memory, 0,
                             std::move(checkerboard_image_info));

    // Create a Material with the checkerboard image.
    Material material(session);
    material.SetColor(255, 100, 100, 255);
    material.SetTexture(checkerboard_image.id());

    // Make a circle, and attach it and the material to a node.
    Circle circle(session, 50);
    circle_node.SetMaterial(material);
    circle_node.SetShape(circle);

    // Make a rounded rect, and attach it and the material to a node.
    RoundedRectangle rrect(session, 200, 300, 20, 20, 80, 10);
    rrect_node_->SetMaterial(material);
    rrect_node_->SetShape(rrect);

    // Translate the circle.
    {
      float translation[3] = {50.f, 50.f, 10.f};
      session->Enqueue(NewSetTranslationOp(circle_node.id(), translation));
    }

    // Translate the EntityNode root.
    {
      float translation[3] = {900.f, 800.f, 10.f};
      session->Enqueue(NewSetTranslationOp(entity_node.id(), translation));
    }

    // Create a Scene, and attach to it the Nodes created above.
    Scene scene(session);
    scene.AddChild(entity_node.id());

    // Create a Camera to view the Scene.
    camera_ = std::make_unique<Camera>(scene);

    // Create a DisplayRenderer that renders the Scene from the viewpoint of the
    // Camera that we just created.
    renderer_ = std::make_unique<DisplayRenderer>(session);
    renderer_->SetCamera(camera_->id());
  }

  void Init() {
    FTL_LOG(INFO) << "Creating new Session";

    // TODO: set up SessionListener.
    mozart2::SessionPtr session;
    scene_manager_->CreateSession(session.NewRequest(), nullptr);
    session_ = std::make_unique<mozart::client::Session>(std::move(session));
    session_->set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Session terminated.";
      loop_->QuitNow();
    });

    // Wait kSessionDuration seconds, and close the session.
    constexpr int kSessionDuration = 40;
    loop_->task_runner()->PostDelayedTask(
        [this] { ReleaseSessionResources(); },
        ftl::TimeDelta::FromSeconds(kSessionDuration));

    // Set up initial scene.
    CreateExampleScene();

    start_time_ = mx_time_get(MX_CLOCK_MONOTONIC);
    camera_anim_start_time_ = start_time_;
    Update(start_time_);
  }

  void Update(uint64_t next_presentation_time) {
    // Translate / rotate the rounded rect.
    {
      double secs = static_cast<double>(next_presentation_time - start_time_) /
                    1'000'000'000;

      float translation[3] = {350.f, 150.f, 10.f};

      translation[0] += sin(secs) * 100.f;
      translation[1] += sin(secs) * 37.f;

      float rotation[4];
      auto quaternion =
          glm::angleAxis(static_cast<float>(secs / 2.0), glm::vec3(0, 0, 1));
      rotation[0] = quaternion.x;
      rotation[1] = quaternion.y;
      rotation[2] = quaternion.z;
      rotation[3] = quaternion.w;

      session_->Enqueue(NewSetTranslationOp(rrect_node_->id(), translation));
      session_->Enqueue(NewSetRotationOp(rrect_node_->id(), rotation));
    }

    // Move the camera.
    {
      double secs = static_cast<double>(next_presentation_time -
                                        camera_anim_start_time_) /
                    1'000'000'000;
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

      glm::vec3 eye_start(1080, 720, 6000);
      glm::vec3 eye_end(0, 10000, 7000);
      glm::vec3 eye =
          glm::mix(eye_start, eye_end, glm::smoothstep(0.f, 1.f, param));

      // Look at the middle of the stage.
      float target[3] = {1080, 720, 0};
      float up[3] = {0, 1, 0};

      session_->Enqueue(NewSetCameraProjectionOp(
          camera_->id(), glm::value_ptr(eye), target, up, glm::radians(15.f)));
    }

    // Present
    session_->Present(0, [this](mozart2::PresentationInfoPtr info) {
      Update(info->presentation_time + info->presentation_interval);
    });
  }

 private:
  void ReleaseSessionResources() {
    FTL_LOG(INFO) << "Closing session.";

    renderer_.reset();
    camera_.reset();
    rrect_node_.reset();

    session_.reset();
  }

  std::unique_ptr<app::ApplicationContext> application_context_;
  mozart2::SceneManagerPtr scene_manager_;
  mtl::MessageLoop* loop_;

  std::unique_ptr<mozart::client::Session> session_;
  std::unique_ptr<mozart::client::ShapeNode> rrect_node_;
  std::unique_ptr<mozart::client::Camera> camera_;
  std::unique_ptr<mozart::client::DisplayRenderer> renderer_;

  uint64_t start_time_ = 0;
  uint64_t camera_anim_start_time_;
  bool camera_anim_returning_ = false;
};

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  HelloSceneManagerApp app;
  loop.task_runner()->PostTask([&app] { app.Init(); });
  loop.task_runner()->PostDelayedTask(
      [&loop] {
        FTL_LOG(INFO) << "Quitting.";
        loop.QuitNow();
      },
      ftl::TimeDelta::FromSeconds(50));
  loop.Run();
  return 0;
}
