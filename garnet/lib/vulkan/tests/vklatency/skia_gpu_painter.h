// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKLATENCY_SKIA_GPU_PAINTER_H_
#define GARNET_LIB_VULKAN_TESTS_VKLATENCY_SKIA_GPU_PAINTER_H_

#include <fuchsia/ui/input/cpp/fidl.h>

#include "garnet/lib/vulkan/tests/vklatency/swapchain.h"
#include "src/lib/fxl/macros.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace examples {

class SkiaGpuPainter {
 public:
  SkiaGpuPainter(Swapchain* swapchain);
  ~SkiaGpuPainter();

  void OnInputEvent(fuchsia::ui::input::InputEvent event);
  void DrawImage();
  bool HasPendingDraw();

 private:
  void PrepareSkSurface(Swapchain::SwapchainImageResources* image);
  void SetImageLayout(Swapchain::SwapchainImageResources* image);

  Swapchain* const vk_swapchain_;
  typedef struct {
    sk_sp<SkSurface> sk_surface;
    std::vector<SkPath> complete_paths;
    std::map<uint32_t /* pointer_id */, SkPath /* path in progress */>
        paths_in_progress;
  } ImageDrawResources;
  std::vector<ImageDrawResources> image_draw_resources_;
  bool pending_draw_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(SkiaGpuPainter);
};

}  // namespace examples

#endif  // GARNET_LIB_VULKAN_TESTS_VKLATENCY_SKIA_GPU_PAINTER_H_
