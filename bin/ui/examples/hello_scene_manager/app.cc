// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/eventpair.h>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/lib/scene/types.h"
#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "apps/mozart/services/scene/ops.fidl.h"
#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "apps/mozart/services/scene/session.fidl.h"
#include "escher/util/image_utils.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

using namespace mozart;

class HelloSceneManagerApp {
 public:
  HelloSceneManagerApp()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        loop_(mtl::MessageLoop::GetCurrent()) {
    // Launch SceneManager.
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "file://system/apps/hello_composer_service";
    launch_info->services = services_.NewRequest();
    application_context_->launcher()->CreateApplication(
        std::move(launch_info), controller_.NewRequest());
    controller_.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Hello SceneManager service terminated.";
      loop_->QuitNow();
    });

    // Connect to the SceneManager service.
    app::ConnectToService(services_.get(), scene_manager_.NewRequest());
  }

  ResourceId NewResourceId() { return ++resource_id_counter_; }

  static ftl::RefPtr<mtl::SharedVmo> CreateSharedVmo(size_t size) {
    mx::vmo vmo;
    mx_status_t status = mx::vmo::create(size, 0u, &vmo);
    if (status != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to create vmo: status=" << status
                     << ", size=" << size;
      return nullptr;
    }

    // Optimization: We will be writing to every page of the buffer, so
    // allocate physical memory for it eagerly.
    status = vmo.op_range(MX_VMO_OP_COMMIT, 0u, size, nullptr, 0u);
    if (status != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to commit all pages of vmo: status=" << status
                     << ", size=" << size;
      return nullptr;
    }

    uint32_t map_flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
    return ftl::MakeRefCounted<mtl::SharedVmo>(std::move(vmo), map_flags);
  }

  fidl::Array<mozart2::OpPtr> CreateLinkAndSampleScene() {
    auto ops = fidl::Array<mozart2::OpPtr>::New(0);

    // Create a Link to attach ourselves to.
    mx::eventpair link_handle1;
    mx::eventpair link_handle2;
    mx::eventpair::create(0, &link_handle1, &link_handle2);
    ResourceId link_id = NewResourceId();
    ops.push_back(NewCreateLinkOp(link_id, std::move(link_handle1)));

    // Create a shape node.
    ResourceId node_id = NewResourceId();
    ops.push_back(NewCreateShapeNodeOp(node_id));

    // Generate a checkerboard.
    size_t checkerboard_width = 8;
    size_t checkerboard_height = 8;
    size_t checkerboard_pixels_size;
    auto checkerboard_pixels = escher::image_utils::NewCheckerboardPixels(
        checkerboard_width, checkerboard_height, &checkerboard_pixels_size);

    auto shared_vmo = CreateSharedVmo(checkerboard_pixels_size);

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

    ops.push_back(
        NewCreateMemoryOp(checkerboard_memory_id, std::move(vmo_copy)));

    // Create an Image to wrap the checkerboard.
    ResourceId checkerboard_image_id = NewResourceId();
    ops.push_back(
        NewCreateImageOp(checkerboard_image_id, checkerboard_memory_id, 0,
                         mozart2::ImageInfo::PixelFormat::BGRA_8,
                         mozart2::ImageInfo::Tiling::LINEAR, checkerboard_width,
                         checkerboard_height, checkerboard_width));

    // Create a Material with the checkerboard image.
    ResourceId material_id = NewResourceId();
    ops.push_back(NewCreateMaterialOp(material_id, checkerboard_image_id, 255,
                                      100, 100, 255));

    // Make the shape a circle.
    ResourceId shape_id = NewResourceId();
    ops.push_back(NewCreateCircleOp(shape_id, 50.f));

    ops.push_back(NewSetMaterialOp(node_id, material_id));
    ops.push_back(NewSetShapeOp(node_id, shape_id));

    // Translate the circle.
    float translation[3] = {50.f, 50.f, 10.f};
    ops.push_back(NewSetTransformOp(node_id, translation,
                                    kOnesFloat3,        // scale
                                    kZeroesFloat3,      // anchor point
                                    kQuaternionDefault  // rotation
                                    ));
    // Attach the circle to the Link.
    ops.push_back(NewAddChildOp(link_id, node_id));

    return ops;
  }

  void Update() {
    FTL_LOG(INFO) << "Creating new Session";
    mozart2::SessionPtr session;
    // TODO: set up SessionListener.
    scene_manager_->CreateSession(session.NewRequest(), nullptr);

    auto ops = CreateLinkAndSampleScene();

    session->Enqueue(std::move(ops));

    // Present
    // TODO: this does not do anything yet.
    session->Present(fidl::Array<mx::event>::New(0),
                     fidl::Array<mx::event>::New(0));

    session.set_connection_error_handler([this] {
      FTL_LOG(INFO) << "Session terminated.";
      loop_->QuitNow();
    });

    // Wait kSessionDuration seconds, and close the session.
    constexpr int kSessionDuration = 10;
    loop_->task_runner()->PostDelayedTask(
        ftl::MakeCopyable([session = std::move(session)]() {
          // Allow SessionPtr to go out of scope, thus closing the
          // session.
          FTL_LOG(INFO) << "Closing session.";
        }),
        ftl::TimeDelta::FromSeconds(kSessionDuration));
  }

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  app::ApplicationControllerPtr controller_;
  app::ServiceProviderPtr services_;
  mozart2::ComposerPtr scene_manager_;
  mtl::MessageLoop* loop_;
  ResourceId resource_id_counter_ = 0;
};

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  HelloSceneManagerApp app;
  loop.task_runner()->PostDelayedTask([&app] { app.Update(); },
                                      ftl::TimeDelta::FromSeconds(5));
  loop.task_runner()->PostDelayedTask(
      [&loop] {
        FTL_LOG(INFO) << "Quitting.";
        loop.QuitNow();
      },
      ftl::TimeDelta::FromSeconds(25));
  loop.Run();
  return 0;
}
