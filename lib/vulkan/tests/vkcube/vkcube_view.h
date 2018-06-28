// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VKCUBE_VIEW_H
#define VKCUBE_VIEW_H

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/fxl/macros.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"

class VkCubeView : public mozart::BaseView {
 public:
  using ResizeCallback = fit::function<void(
        float width, float height,
        fidl::InterfaceHandle<fuchsia::images::ImagePipe> interface_request)>;

  VkCubeView(
      ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner> view_owner_request,
      ResizeCallback resize_callback);
  ~VkCubeView() override;

 private:
  // |BaseView|:
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  fuchsia::math::SizeF size_;
  fuchsia::math::Size physical_size_;
  scenic::ShapeNode pane_node_;
  ResizeCallback resize_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VkCubeView);
};

#endif  // VKCUBE_VIEW_H
