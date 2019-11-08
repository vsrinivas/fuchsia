// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/examples/video_display/simple_camera_view.h"

// clang-format off
#include "lib/zx/time.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <lib/ui/scenic/cpp/commands.h>
#include <glm/gtc/type_ptr.hpp>

namespace video_display {

namespace {
constexpr uint32_t kShapeWidth = 640;
constexpr uint32_t kShapeHeight = 480;
constexpr float kDisplayHeight = 50;
constexpr float kInitialWindowXPos = 320;
constexpr float kInitialWindowYPos = 240;
}  // namespace

static const std::string kSimpleCameraServiceUrl =
    "fuchsia-pkg://fuchsia.com/simple_camera_server_cpp#meta/"
    "simple_camera_server_cpp.cmx";

static const uint32_t camera_id = 0;  // 0 -> fake camera, 1 -> real camera
static const auto kRectSize = 80;

SimpleCameraView::SimpleCameraView(scenic::ViewContext view_context)
    : BaseView(std::move(view_context), "Video Display Example"), node_(session()) {
  FX_VLOGS(4) << "Creating video_display View";

  // Create an ImagePipe and pass one end to the Session:
  fidl::InterfaceHandle<fuchsia::images::ImagePipe> image_pipe_handle;
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipeCmd(image_pipe_id, image_pipe_handle.NewRequest()));

  // Create a material that has our image pipe mapped onto it:
  scenic::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Connect to the simple camera service:
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kSimpleCameraServiceUrl;
  simple_camera_provider_ =
      sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);

  fuchsia::sys::LauncherPtr launcher;
  component_context()->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());

  simple_camera_ = simple_camera_provider_->Connect<fuchsia::simplecamera::SimpleCamera>();

  // Now pass the other end of the image pipe to the simple camera interface:
  simple_camera_->ConnectToCamera(camera_id, std::move(image_pipe_handle));
  simple_camera_.set_error_handler([](zx_status_t error) {
    if (error) {
      FX_PLOGS(FATAL, error) << "Camera connection failed";
    }
  });

  // Create a rounded-rect shape to display the camera image on.
  scenic::RoundedRectangle shape(session(), kShapeWidth, kShapeHeight, kRectSize, kRectSize,
                                 kRectSize, kRectSize);

  node_.SetShape(shape);
  node_.SetMaterial(material);
  root_node().AddChild(node_);
  // Translation of 0, 0 is the middle of the screen
  node_.SetTranslation(kInitialWindowXPos, kInitialWindowYPos, -kDisplayHeight);
  InvalidateScene();
}

void SimpleCameraView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  // Compute the amount of time that has elapsed since the view was created.
  auto seconds = zx::duration(presentation_info.presentation_time).to_secs();

  // Compute the translation for the window to swirl around the screen.
  // Why do this?  Well, this is an example of what a View can do, and it helps
  // debug the camera to know if scenic is still running.

  const double kHalfWidth = logical_size().x * 0.5f;
  const double kHalfHeight = logical_size().y * 0.5f;
  const double kXTranslation = kHalfWidth * (1. + .1 * sin(seconds * 0.8));
  const double kYTranslation = kHalfHeight * (1. + .1 * sin(seconds * 0.6));

  node_.SetTranslation(static_cast<float>(kXTranslation), static_cast<float>(kYTranslation),
                       -kDisplayHeight);

  // The rounded-rectangles are constantly animating; invoke InvalidateScene()
  // to guarantee that OnSceneInvalidated() will be called again.
  InvalidateScene();
}
}  // namespace video_display
