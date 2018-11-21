// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IMAGE_PIPE_SURFACE_H
#define IMAGE_PIPE_SURFACE_H

#include <fuchsia/images/cpp/fidl.h>
#include <vulkan/vulkan.h>

#include <vector>

namespace image_pipe_swapchain {

struct SupportedImageProperties {
  std::vector<VkSurfaceFormatKHR> formats;
};

class ImagePipeSurface {
 public:
  ImagePipeSurface() {
    std::vector<VkSurfaceFormatKHR> formats(
        {{VK_FORMAT_B8G8R8A8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR}});
    supported_image_properties_ = {formats};
  }

  virtual ~ImagePipeSurface() = default;

  SupportedImageProperties& supported_image_properties() {
    return supported_image_properties_;
  }

  virtual bool CanPresentPendingImage() { return true; }
  virtual bool UseScanoutExtension() { return false; }

  virtual bool GetSize(uint32_t* width_out, uint32_t* height_out) {
    return false;
  }

  uint32_t next_image_id() {
    if (++next_image_id_ == 0) {
      ++next_image_id_;
    }
    return next_image_id_;
  }

  virtual void AddImage(uint32_t image_id,
                        fuchsia::images::ImageInfo image_info, zx::vmo buffer,
                        uint64_t size_bytes) = 0;
  virtual void RemoveImage(uint32_t image_id) = 0;
  virtual void PresentImage(uint32_t image_id,
                            fidl::VectorPtr<zx::event> acquire_fences,
                            fidl::VectorPtr<zx::event> release_fences) = 0;

 private:
  SupportedImageProperties supported_image_properties_;
  uint32_t next_image_id_ = UINT32_MAX - 1;  // Exercise rollover
};

}  // namespace image_pipe_swapchain

#endif  // IMAGE_PIPE_SURFACE_H