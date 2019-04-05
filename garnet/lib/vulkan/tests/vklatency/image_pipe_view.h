// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKLATENCY_IMAGE_PIPE_VIEW_H_
#define GARNET_LIB_VULKAN_TESTS_VKLATENCY_IMAGE_PIPE_VIEW_H_

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/ui/base_view/cpp/base_view.h>

#include <memory>

#include "garnet/lib/vulkan/tests/vklatency/skia_gpu_painter.h"
#include "garnet/lib/vulkan/tests/vklatency/swapchain.h"
#include "src/lib/fxl/macros.h"

namespace examples {

class ImagePipeView : public scenic::BaseView {
 public:
  explicit ImagePipeView(scenic::ViewContext view_context);
  ~ImagePipeView() override = default;

 private:
  // |scenic::BaseView|
  void Initialize();
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;
  void OnInputEvent(fuchsia::ui::input::InputEvent event) override;
  void OnScenicError(::std::string error) override;

  zx::channel image_pipe_endpoint_;
  fuchsia::ui::gfx::vec3 size_;
  fuchsia::ui::gfx::vec3 physical_size_;
  scenic::ShapeNode canvas_node_;

  Swapchain vk_swapchain_;
  // TODO(emircan): Add other implementation based on command-line.
  std::unique_ptr<SkiaGpuPainter> painter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ImagePipeView);
};

}  // namespace examples

#endif  // GARNET_LIB_VULKAN_TESTS_VKLATENCY_IMAGE_PIPE_VIEW_H_
