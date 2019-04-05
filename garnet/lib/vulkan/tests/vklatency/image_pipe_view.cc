// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/vulkan/tests/vklatency/image_pipe_view.h"

#include <hid/usages.h>
#include <lib/ui/scenic/cpp/commands.h>

#include "src/lib/fxl/logging.h"

namespace examples {

ImagePipeView::ImagePipeView(scenic::ViewContext view_context)
    : BaseView(std::move(view_context), "gfxlatency_on_scenic"),
      canvas_node_(session()) {
  zx::channel remote_endpoint;
  zx::channel::create(0, &image_pipe_endpoint_, &remote_endpoint);

  // Create an ImagePipe
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipeCmd(
      image_pipe_id, fidl::InterfaceRequest<fuchsia::images::ImagePipe>(
                         std::move(remote_endpoint))));

  // Create a material that has our image pipe mapped onto it:
  scenic::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rectangle shape to display the Image on.
  canvas_node_.SetMaterial(material);
  root_node().AddChild(canvas_node_);
}

void ImagePipeView::Initialize() {
  size_ = logical_size();
  physical_size_ = physical_size();

  scenic::Rectangle canvas_shape(session(), logical_size().x, logical_size().y);
  canvas_node_.SetShape(canvas_shape);
  canvas_node_.SetTranslation(logical_size().x * 0.5, logical_size().y * 0.5,
                              0);
  const bool rv = vk_swapchain_.Initialize(std::move(image_pipe_endpoint_),
                                           logical_size().x, logical_size().y);
  FXL_CHECK(rv);
  painter_ = std::make_unique<SkiaGpuPainter>(&vk_swapchain_, logical_size().x,
                                              logical_size().y);
}

void ImagePipeView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_metrics())
    return;
  if (!painter_)
    Initialize();

  painter_->DrawImage();
}

void ImagePipeView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  painter_->OnInputEvent(std::move(event));
  if (painter_->HasPendingDraw())
    InvalidateScene();
}

void ImagePipeView::OnScenicError(::std::string error) {
  FXL_LOG(ERROR) << "Scenic Error " << error;
}

}  // namespace examples
