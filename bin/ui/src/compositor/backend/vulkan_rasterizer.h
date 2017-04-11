// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_BACKEND_VULKAN_RASTERIZER_H_
#define APPS_MOZART_SRC_COMPOSITOR_BACKEND_VULKAN_RASTERIZER_H_

#include "apps/mozart/src/compositor/backend/framebuffer.h"
#include "apps/mozart/src/compositor/backend/framebuffer_output.h"
#include "apps/mozart/src/compositor/backend/rasterizer.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace mtl {
class Thread;
}

namespace vulkan {
class VulkanWindow;
}

namespace compositor {

// Rasterizer backed by a Magma surface. Uses Skia Vulkan backend.
class VulkanRasterizer : public Rasterizer {
 public:
  explicit VulkanRasterizer(
      const RasterizeFrameFinishedCallback& frame_finished_callback);
  ~VulkanRasterizer() override;

  void DrawFrame(ftl::RefPtr<RenderFrame> frame,
                 uint32_t frame_number,
                 ftl::TimePoint submit_time) override;

  bool Initialize(mx_display_info_t* mx_display_info) override;

 private:
  static std::unique_ptr<vulkan::VulkanWindow> InitializeVulkanWindow(
      int32_t surface_width,
      int32_t surface_height);

  std::unique_ptr<vulkan::VulkanWindow> window_;
  std::unique_ptr<Framebuffer> framebuffer_;
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_BACKEND_VULKAN_RASTERIZER_H_
