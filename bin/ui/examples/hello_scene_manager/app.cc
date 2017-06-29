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

#include <glm/gtx/quaternion.hpp>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "escher/util/image_utils.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/lib/scene/types.h"
#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "apps/mozart/services/scene/ops.fidl.h"
#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "apps/mozart/services/scene/session.fidl.h"
#include "apps/mozart/src/scene/tests/util.h"

using namespace mozart;

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

  ResourceId NewResourceId() { return ++resource_id_counter_; }

  fidl::Array<mozart2::OpPtr> CreateExampleScene() {
    auto ops = fidl::Array<mozart2::OpPtr>::New(0);

    ResourceId entity_node_id = NewResourceId();
    ops.push_back(NewCreateEntityNodeOp(entity_node_id));

    // Create two shape nodes, one for a circle and one for a rounded-rect.
    // Remember the ID of the rounded-rect node, because we're going to animate
    // it.
    ResourceId circle_node_id = NewResourceId();
    ops.push_back(NewCreateShapeNodeOp(circle_node_id));

    rrect_node_id_ = NewResourceId();
    ops.push_back(NewCreateShapeNodeOp(rrect_node_id_));

    // Immediately attach them to the root.
    ops.push_back(NewAddChildOp(entity_node_id, circle_node_id));
    ops.push_back(NewAddChildOp(entity_node_id, rrect_node_id_));

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

    ResourceId checkerboard_memory_id = NewResourceId();

    // Duplicate the VMO handle.
    mx::vmo vmo_copy;
    auto status = shared_vmo->vmo().duplicate(MX_RIGHT_SAME_RIGHTS, &vmo_copy);
    if (status) {
      FTL_LOG(ERROR) << "Failed to duplicate vmo handle.";
      return nullptr;
    }

    ops.push_back(NewCreateMemoryOp(checkerboard_memory_id, std::move(vmo_copy),
                                    mozart2::MemoryType::HOST_MEMORY));

    // Create an Image to wrap the checkerboard.
    ResourceId checkerboard_image_id = NewResourceId();
    const size_t bytes_per_pixel = 4u;
    ops.push_back(NewCreateImageOp(
        checkerboard_image_id, checkerboard_memory_id, 0,
        mozart2::ImageInfo::PixelFormat::BGRA_8,
        mozart2::ImageInfo::ColorSpace::SRGB,
        mozart2::ImageInfo::Tiling::LINEAR, checkerboard_width,
        checkerboard_height, checkerboard_width * bytes_per_pixel));

    // Create a Material with the checkerboard image.
    ResourceId material_id = NewResourceId();
    ops.push_back(NewCreateMaterialOp(material_id));
    ops.push_back(NewSetColorOp(material_id, 255, 100, 100, 255));
    ops.push_back(NewSetTextureOp(material_id, checkerboard_image_id));

    // Make a circle, and attach it and the material to a node.
    ResourceId circle_id = NewResourceId();
    ops.push_back(NewCreateCircleOp(circle_id, 50.f));

    ops.push_back(NewSetMaterialOp(circle_node_id, material_id));
    ops.push_back(NewSetShapeOp(circle_node_id, circle_id));

    // Make a rounded rect, and attach it and the material to a node.
    ResourceId rrect_id = NewResourceId();
    ops.push_back(
        NewCreateRoundedRectangleOp(rrect_id, 200, 300, 20, 20, 80, 10));

    ops.push_back(NewSetMaterialOp(rrect_node_id_, material_id));
    ops.push_back(NewSetShapeOp(rrect_node_id_, rrect_id));

    // Translate the circle.
    {
      float translation[3] = {50.f, 50.f, 10.f};
      ops.push_back(NewSetTranslationOp(circle_node_id, translation));
    }

    // Translate the EntityNode root.
    {
      float translation[3] = {900.f, 800.f, 10.f};
      ops.push_back(NewSetTranslationOp(entity_node_id, translation));
    }

    // Create a Scene, and attach to it the Nodes created above.
    ResourceId scene_id = NewResourceId();
    ops.push_back(NewCreateSceneOp(scene_id));
    ops.push_back(NewAddChildOp(scene_id, entity_node_id));

    // Create a Camera to view the Scene.
    ResourceId camera_id = NewResourceId();
    ops.push_back(NewCreateCameraOp(camera_id, scene_id));

    // Create a DisplayRenderer that renders the Scene from the viewpoint of the
    // Camera that we just created.
    ResourceId renderer_id = NewResourceId();
    ops.push_back(NewCreateDisplayRendererOp(renderer_id));
    ops.push_back(NewSetCameraOp(renderer_id, camera_id));

    return ops;
  }

  void Init() {
    FTL_LOG(INFO) << "Creating new Session";

    // TODO: set up SessionListener.
    scene_manager_->CreateSession(session_.NewRequest(), nullptr);
    session_.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Session terminated.";
      loop_->QuitNow();
    });

    // Wait kSessionDuration seconds, and close the session.
    constexpr int kSessionDuration = 20;
    loop_->task_runner()->PostDelayedTask(
        [this] {
          // Allow SessionPtr to go out of scope, thus closing the
          // session.
          mozart2::SessionPtr session(std::move(session_));
          FTL_LOG(INFO) << "Closing session.";
        },
        ftl::TimeDelta::FromSeconds(kSessionDuration));

    // Set up initial scene.
    session_->Enqueue(CreateExampleScene());

    start_time_ = mx_time_get(MX_CLOCK_MONOTONIC);
    Update(start_time_);
  }

  void Update(uint64_t next_presentation_time) {
    auto ops = fidl::Array<mozart2::OpPtr>::New(0);

    // Translate / rotate the rounded rect.
    {
      float translation[3] = {350.f, 150.f, 10.f};
      float rotation[4];

      double secs = static_cast<double>(next_presentation_time - start_time_) /
                    1'000'000'000;
      translation[0] += sin(secs) * 100.f;
      translation[1] += sin(secs) * 37.f;

      auto quaternion =
          glm::angleAxis(static_cast<float>(secs / 2.0), glm::vec3(0, 0, 1));
      rotation[0] = quaternion.x;
      rotation[1] = quaternion.y;
      rotation[2] = quaternion.z;
      rotation[3] = quaternion.w;

      ops.push_back(NewSetTranslationOp(rrect_node_id_, translation));
      ops.push_back(NewSetRotationOp(rrect_node_id_, rotation));
    }

    session_->Enqueue(std::move(ops));

    // Present
    session_->Present(
        0, fidl::Array<mx::event>::New(0), fidl::Array<mx::event>::New(0),
        [this](mozart2::PresentationInfoPtr info) {
          Update(info->presentation_time + info->presentation_interval);
        });
  }

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  app::ApplicationControllerPtr controller_;
  app::ServiceProviderPtr services_;
  mozart2::SceneManagerPtr scene_manager_;
  mtl::MessageLoop* loop_;
  ResourceId resource_id_counter_ = 0;
  mozart2::SessionPtr session_;
  ResourceId rrect_node_id_ = 0;
  uint64_t start_time_ = 0;
};

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  HelloSceneManagerApp app;
  loop.task_runner()->PostDelayedTask([&app] { app.Init(); },
                                      ftl::TimeDelta::FromSeconds(2));
  loop.task_runner()->PostDelayedTask(
      [&loop] {
        FTL_LOG(INFO) << "Quitting.";
        loop.QuitNow();
      },
      ftl::TimeDelta::FromSeconds(25));
  loop.Run();
  return 0;
}
