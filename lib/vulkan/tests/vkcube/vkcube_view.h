// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VKCUBE_VIEW_H
#define VKCUBE_VIEW_H

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/fxl/macros.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/view_framework/base_view.h"

class VkCubeView : public mozart::BaseView {
 public:
  using ResizeCallback = fit::function<void(float width, float height)>;

  VkCubeView(
      ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner> view_owner_request,
      ResizeCallback resize_callback);
  ~VkCubeView() override;

  zx::channel TakeImagePipeChannel() { return std::move(image_pipe_endpoint_); }

 private:
  // |BaseView|:
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  fuchsia::math::SizeF size_;
  fuchsia::math::Size physical_size_;
  scenic::ShapeNode pane_node_;
  scenic::Material pane_material_;
  ResizeCallback resize_callback_;
  zx::channel image_pipe_endpoint_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VkCubeView);
};

#endif  // VKCUBE_VIEW_H
