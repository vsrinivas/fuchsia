// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vkcube_view.h"
#include "garnet/public/lib/ui/scenic/fidl_helpers.h"

VkCubeView::VkCubeView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    std::function<void(float width, float height,
                       fidl::InterfaceHandle<scenic::ImagePipe> interface_request)>
        resize_callback)
    : BaseView(std::move(view_manager), std::move(view_owner_request), "vkcube"),
      pane_node_(session()), resize_callback_(resize_callback)
{
}

VkCubeView::~VkCubeView() {}

void VkCubeView::OnSceneInvalidated(scenic::PresentationInfoPtr presentation_info)
{
    if (size_.Equals(logical_size()))
        return;

    size_ = logical_size();

    scenic_lib::Rectangle pane_shape(session(), logical_size().width, logical_size().height);
    scenic_lib::Material pane_material(session());

    pane_node_.SetShape(pane_shape);
    pane_node_.SetMaterial(pane_material);
    pane_node_.SetTranslation(logical_size().width * 0.5, logical_size().height * 0.5, 0);
    parent_node().AddChild(pane_node_);

    zx::channel endpoint0;
    zx::channel endpoint1;
    zx::channel::create(0, &endpoint0, &endpoint1);

    uint32_t image_pipe_id = session()->AllocResourceId();
    session()->Enqueue(scenic_lib::NewCreateImagePipeOp(
        image_pipe_id, fidl::InterfaceRequest<scenic::ImagePipe>(std::move(endpoint1))));
    pane_material.SetTexture(image_pipe_id);
    session()->ReleaseResource(image_pipe_id);
    session()->Present(zx_time_get(ZX_CLOCK_MONOTONIC), [](scenic::PresentationInfoPtr info) {});

    resize_callback_(logical_size().width, logical_size().height,
                     fidl::InterfaceHandle<scenic::ImagePipe>(std::move(endpoint0)));
}
