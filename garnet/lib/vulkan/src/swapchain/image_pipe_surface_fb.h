// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_SRC_SWAPCHAIN_IMAGE_PIPE_SURFACE_FB_H_
#define GARNET_LIB_VULKAN_SRC_SWAPCHAIN_IMAGE_PIPE_SURFACE_FB_H_

#include <map>
#include "image_pipe_surface.h"

namespace image_pipe_swapchain {

// An implementation of ImagePipeSurface based on the zircon framebuffer api.
class ImagePipeSurfaceFb : public ImagePipeSurface {
 public:
  ImagePipeSurfaceFb();
  ~ImagePipeSurfaceFb() override;

  bool CanPresentPendingImage() override { return false; }
  bool UseScanoutExtension() override { return true; }

  bool GetSize(uint32_t* width_out, uint32_t* height_out) override;

  void AddImage(uint32_t image_id, fuchsia::images::ImageInfo image_info,
                zx::vmo buffer, uint64_t size_bytes) override;

  void RemoveImage(uint32_t image_id) override;

  void PresentImage(uint32_t image_id,
                    std::vector<zx::event> acquire_fences,
                    std::vector<zx::event> release_fences) override;

 private:
  std::map<uint64_t, uint64_t> image_id_map;
};

}  // namespace image_pipe_swapchain

#endif  // GARNET_LIB_VULKAN_SRC_SWAPCHAIN_IMAGE_PIPE_SURFACE_FB_H_
