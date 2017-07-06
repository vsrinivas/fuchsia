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

#include "apps/mozart/lib/scene/client/host_memory.h"
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

static constexpr uint32_t kScreenWidth = 2160;
static constexpr uint32_t kScreenHeight = 1440;

static constexpr float kPaneMargin = 100.f;
static constexpr float kPaneHeight = kScreenHeight - 2 * kPaneMargin;
static constexpr float kPaneWidth = (kScreenWidth - 3 * kPaneMargin) / 2.f;

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

  void InitCheckerboardMaterial(Material* uninitialized_material) {
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
    auto checkerboard_image_info = mozart2::ImageInfo::New();
    checkerboard_image_info->width = checkerboard_width;
    checkerboard_image_info->height = checkerboard_height;
    const size_t kBytesPerPixel = 4u;
    checkerboard_image_info->stride = checkerboard_width * kBytesPerPixel;
    checkerboard_image_info->pixel_format =
        mozart2::ImageInfo::PixelFormat::BGRA_8;
    checkerboard_image_info->color_space = mozart2::ImageInfo::ColorSpace::SRGB;
    checkerboard_image_info->tiling = mozart2::ImageInfo::Tiling::LINEAR;

    HostImage checkerboard_image(checkerboard_memory, 0,
                                 std::move(checkerboard_image_info));

    uninitialized_material->SetTexture(checkerboard_image.id());
  }

  void CreateExampleScene() {
    auto session = session_.get();

    // Create an EntityNode to serve as the scene root.
    EntityNode root_node(session);

    // The root node will enclose two "panes", each with a rounded-rect part
    // that acts as a background clipper.
    RoundedRectangle pane_shape(session, kPaneWidth, kPaneHeight, 20, 20, 80,
                                10);
    Material pane_material(session);
    pane_material.SetColor(120, 120, 255, 255);

    EntityNode pane_node_1(session);
    ShapeNode pane_bg_1(session);
    pane_bg_1.SetShape(pane_shape);
    pane_bg_1.SetMaterial(pane_material);
    pane_node_1.AddPart(pane_bg_1);
    pane_node_1.SetTranslation(kPaneMargin + kPaneWidth * 0.5,
                               kPaneMargin + kPaneHeight * 0.5, 20);
    pane_node_1.SetClip(0, true);
    root_node.AddChild(pane_node_1);

    EntityNode pane_node_2(session);
    ShapeNode pane_bg_2(session);
    pane_bg_2.SetShape(pane_shape);
    pane_bg_2.SetMaterial(pane_material);
    pane_node_2.AddPart(pane_bg_2);
    pane_node_2.SetTranslation(kPaneMargin * 2 + kPaneWidth * 1.5,
                               kPaneMargin + kPaneHeight * 0.5, 20);
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

    // Create a Scene, and attach to it the Nodes created above.
    Scene scene(session);
    scene.AddChild(root_node.id());

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

      rrect_node_->SetTranslation(sin(secs * 0.8) * 500.f,
                                  sin(secs * 0.6) * 570.f, 10.f);

      auto quaternion =
          glm::angleAxis(static_cast<float>(secs / 2.0), glm::vec3(0, 0, 1));
      rrect_node_->SetRotation(quaternion.x, quaternion.y, quaternion.z,
                               quaternion.w);
    }

    // Translate the clip-circles.
    {
      double secs = static_cast<double>(next_presentation_time - start_time_) /
                    1'000'000'000;

      float offset1 = sin(secs * 0.8) * 300.f;
      float offset2 = cos(secs * 0.8) * 300.f;

      clipper_1_->SetTranslation(offset1, offset2 * 3, -5);
      clipper_2_->SetTranslation(offset2, offset1 * 2, -4);
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

      // Animate the eye position.
      glm::vec3 eye_start(1080, 720, 6000);
      glm::vec3 eye_end(0, 10000, 7000);
      glm::vec3 eye =
          glm::mix(eye_start, eye_end, glm::smoothstep(0.f, 1.f, param));

      // Always look at the middle of the stage.
      float target[3] = {1080, 720, 0};
      float up[3] = {0, 1, 0};

      camera_->SetProjection(glm::value_ptr(eye), target, up,
                             glm::radians(15.f));
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
  std::unique_ptr<mozart::client::ShapeNode> clipper_1_;
  std::unique_ptr<mozart::client::ShapeNode> clipper_2_;
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
