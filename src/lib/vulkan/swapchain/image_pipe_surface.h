// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IMAGE_PIPE_SURFACE_H
#define IMAGE_PIPE_SURFACE_H

#include <fuchsia/images/cpp/fidl.h>

#include <vector>

#include <vulkan/vulkan.h>

struct VkLayerDispatchTable_;
typedef struct VkLayerDispatchTable_ VkLayerDispatchTable;

namespace image_pipe_swapchain {

struct SupportedImageProperties {
  std::vector<VkSurfaceFormatKHR> formats;
};

// An abstract surface that must implement AddImage, RemoveImage, and
// PresentImage. These methods are defined as per the ImagePipe fidl interface
// (see image_pipe.fidl).
class ImagePipeSurface {
 public:
  struct ImageInfo {
    VkImage image{};
    VkDeviceMemory memory{};
    uint32_t image_id{};
  };

  ImagePipeSurface() {}

  virtual ~ImagePipeSurface() = default;

  VkFlags SupportedUsage() {
    return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
           VK_IMAGE_USAGE_STORAGE_BIT;
  }

  virtual bool Init() { return true; }
  virtual bool CanPresentPendingImage() { return true; }

  virtual bool GetSize(uint32_t* width_out, uint32_t* height_out) { return false; }

  virtual bool IsLost() { return false; }
  virtual bool CreateImage(VkDevice device, VkLayerDispatchTable* pDisp, VkFormat format,
                           VkImageUsageFlags usage, VkSwapchainCreateFlagsKHR swapchain_flags,
                           fuchsia::images::ImageInfo image_info, uint32_t image_count,
                           const VkAllocationCallbacks* pAllocator,
                           std::vector<ImageInfo>* image_info_out) = 0;
  virtual void RemoveImage(uint32_t image_id) = 0;
  virtual void PresentImage(uint32_t image_id, std::vector<zx::event> acquire_fences,
                            std::vector<zx::event> release_fences) = 0;

  virtual SupportedImageProperties& GetSupportedImageProperties() = 0;

 protected:
  uint32_t next_image_id() {
    if (++next_image_id_ == 0) {
      ++next_image_id_;
    }
    return next_image_id_;
  }

 private:
  uint32_t next_image_id_ = UINT32_MAX - 1;  // Exercise rollover
};

}  // namespace image_pipe_swapchain

#endif  // IMAGE_PIPE_SURFACE_H
