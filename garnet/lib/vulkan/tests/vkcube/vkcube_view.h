// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKCUBE_VKCUBE_VIEW_H_
#define GARNET_LIB_VULKAN_TESTS_VKCUBE_VKCUBE_VIEW_H_

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/logging.h>

#include "lib/ui/base_view/cpp/base_view.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "src/lib/fxl/macros.h"

class VkCubeView : public scenic::BaseView {
 public:
  using ResizeCallback = fit::function<void(float width, float height)>;

  VkCubeView(scenic::ViewContext context, ResizeCallback resize_callback);
  ~VkCubeView() override;

  zx::channel TakeImagePipeChannel() { return std::move(image_pipe_endpoint_); }

 private:
  // |scenic::BaseView|
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override {
    FXL_LOG(ERROR) << "Scenic Error " << error;
  }

  fuchsia::ui::gfx::vec3 size_;
  fuchsia::ui::gfx::vec3 physical_size_;
  scenic::ShapeNode pane_node_;
  scenic::Material pane_material_;
  ResizeCallback resize_callback_;
  zx::channel image_pipe_endpoint_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VkCubeView);
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKCUBE_VKCUBE_VIEW_H_
