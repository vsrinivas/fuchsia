// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VKCUBE_VIEW_H
#define VKCUBE_VIEW_H

#include <fuchsia/cpp/gfx.h>
#include <fuchsia/cpp/images.h>
#include <fuchsia/cpp/views_v1_token.h>

#include "lib/fxl/macros.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"

class VkCubeView : public mozart::BaseView {
public:
    VkCubeView(views_v1::ViewManagerPtr view_manager,
               fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
               std::function<void(float width, float height,
                                  fidl::InterfaceHandle<images::ImagePipe> interface_request)>
                   resize_callback);
    ~VkCubeView() override;

private:
    // |BaseView|:
    void OnSceneInvalidated(images::PresentationInfo presentation_info) override;

    geometry::SizeF size_;
    geometry::Size physical_size_;
    scenic_lib::ShapeNode pane_node_;
    std::function<void(float width, float height,
                       fidl::InterfaceHandle<images::ImagePipe> interface_request)>
        resize_callback_;

    FXL_DISALLOW_COPY_AND_ASSIGN(VkCubeView);
};

#endif // VKCUBE_VIEW_H
