// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <garnet/examples/ui/video_display/simple_camera_view.h>

#if defined(countof)
// TODO(ZX-377): Workaround for compiler error due to Zircon defining countof()
// as a macro.  Redefines countof() using GLM_COUNTOF(), which currently
// provides a more sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif
#include <lib/fxl/log_level.h>
#include <lib/fxl/logging.h>
#include <glm/gtc/type_ptr.hpp>

#include <lib/ui/scenic/fidl_helpers.h>

namespace video_display {

namespace {
constexpr uint32_t kShapeWidth = 640;
constexpr uint32_t kShapeHeight = 480;
constexpr float kDisplayHeight = 50;
constexpr float kInitialWindowXPos = 320;
constexpr float kInitialWindowYPos = 240;
}  // namespace

SimpleCameraView::SimpleCameraView(
    async::Loop* loop, fuchsia::sys::StartupContext* startup_context,
    ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
        view_owner_request,
    bool use_fake_camera)
    : BaseView(std::move(view_manager), std::move(view_owner_request),
               "Video Display Example"),
      node_(session()) {
  FXL_VLOG(4) << "Creating View";
  // Create an ImagePipe and pass one end to the Session:
  fidl::InterfaceHandle<fuchsia::images::ImagePipe> image_pipe_handle;
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic_lib::NewCreateImagePipeCmd(
      image_pipe_id, image_pipe_handle.NewRequest()));

  // Create a material that has our image pipe mapped onto it:
  scenic_lib::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Now pass the other end to the simple camera interface;
  zx_status_t status = video_display_.ConnectToCamera(
      0, std::move(image_pipe_handle), [loop]() { loop->Quit(); });
  if (status != ZX_OK) {
    loop->Quit();
    return;
  }

  // Create a rounded-rect shape to display the camera image on.
  scenic_lib::RoundedRectangle shape(session(), kShapeWidth, kShapeHeight, 80,
                                     80, 80, 80);

  node_.SetShape(shape);
  node_.SetMaterial(material);
  parent_node().AddChild(node_);
  // Translation of 0, 0 is the middle of the screen
  node_.SetTranslation(kInitialWindowXPos, kInitialWindowYPos, kDisplayHeight);
  InvalidateScene();
}

SimpleCameraView::~SimpleCameraView() = default;

void SimpleCameraView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  // Compute the amount of time that has elapsed since the view was created.
  double seconds =
      static_cast<double>(presentation_info.presentation_time) / 1'000'000'000;

  const float kHalfWidth = logical_size().width * 0.5f;
  const float kHalfHeight = logical_size().height * 0.5f;

  // Compute the translation for the window to swirl around the screen.
  // Why do this?  Well, this is an example of what a View can do, and it helps
  // debug the camera to know if scenic is still running.
  node_.SetTranslation(kHalfWidth * (1. + .1 * sin(seconds * 0.8)),
                       kHalfHeight * (1. + .1 * sin(seconds * 0.6)),
                       kDisplayHeight);

  // The rounded-rectangles are constantly animating; invoke InvalidateScene()
  // to guarantee that OnSceneInvalidated() will be called again.
  InvalidateScene();
}
}  // namespace video_display
