// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vkcube_view.h"
#include "lib/ui/geometry/cpp/geometry_util.h"
#include "lib/ui/scenic/fidl_helpers.h"

VkCubeView::VkCubeView(
    ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request,
    ResizeCallback resize_callback)
    : BaseView(std::move(view_manager), std::move(view_owner_request),
               "vkcube"),
      pane_node_(session()),
      pane_material_(session()),
      resize_callback_(std::move(resize_callback)) {
  zx::channel remote_endpoint;
  zx::channel::create(0, &image_pipe_endpoint_, &remote_endpoint);

  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipeCmd(
      image_pipe_id, fidl::InterfaceRequest<fuchsia::images::ImagePipe>(
                         std::move(remote_endpoint))));
  pane_material_.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  pane_node_.SetMaterial(pane_material_);
  parent_node().AddChild(pane_node_);
}

VkCubeView::~VkCubeView() {}

void VkCubeView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_metrics())
    return;
  if (size_ == logical_size() && physical_size_ == physical_size())
    return;

  size_ = logical_size();
  physical_size_ = physical_size();

  scenic::Rectangle pane_shape(session(), logical_size().width,
                               logical_size().height);
  pane_node_.SetShape(pane_shape);
  pane_node_.SetTranslation(logical_size().width * 0.5,
                            logical_size().height * 0.5, 0);

  // No need to Present on session; base_view will present after calling
  // OnSceneInvalidated.

  resize_callback_(physical_size().width, physical_size().height);
}
