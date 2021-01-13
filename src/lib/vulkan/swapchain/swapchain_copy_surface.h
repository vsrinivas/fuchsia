// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VULKAN_SWAPCHAIN_SWAPCHAIN_COPY_SURFACE_H_
#define SRC_LIB_VULKAN_SWAPCHAIN_SWAPCHAIN_COPY_SURFACE_H_

#include <map>
#include <vector>

#include "image_pipe_surface.h"

namespace image_pipe_swapchain {

// This surface isn't standalone, it depends on another swapchain implementation.
// Its purpose is to insert a layer of buffering that decouples a swapchain-based
// application loop from the presentation timing of the underlying swapchain.
//
// Using ImagePipe surfaces directly, application throughput can suffer in some cases:
// - with ImagePipeSurfaceAsync, an Imagepipe consumer may defer composition
// - with ImagePipeSurfaceDisplay, double buffered frame rates under 60Hz are capped to 30Hz
//
// This intermediary copy step incurs some overhead but can be useful for benchmarking
// onscreen performance vs other platforms.
//
// To use, add this copy layer before any backend swapchain layer.
//
class SwapchainCopySurface : public ImagePipeSurface {
 public:
  SwapchainCopySurface();

#if defined(VK_USE_PLATFORM_FUCHSIA)
  bool OnCreateSurface(VkInstance instance, VkLayerInstanceDispatchTable* dispatch_table,
                       const VkImagePipeSurfaceCreateInfoFUCHSIA* pCreateInfo,
                       const VkAllocationCallbacks* pAllocator) override;
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
  bool OnCreateSurface(VkInstance instance, VkLayerInstanceDispatchTable* dispatch_table,
                       const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
                       const VkAllocationCallbacks* pAllocator) override;
#endif

  void OnDestroySurface(VkInstance instance, VkLayerInstanceDispatchTable* dispatch_table,
                        const VkAllocationCallbacks* pAllocator) override;

  bool OnCreateSwapchain(VkDevice device, LayerData* device_layer_data,
                         const VkSwapchainCreateInfoKHR* pCreateInfo,
                         const VkAllocationCallbacks* pAllocator) override;

  void OnDestroySwapchain(VkDevice device, const VkAllocationCallbacks* pAllocator) override;

  bool CreateImage(VkDevice device, VkLayerDispatchTable* pDisp, VkFormat format,
                   VkImageUsageFlags usage, VkSwapchainCreateFlagsKHR swapchain_flags,
                   VkExtent2D extent, uint32_t image_count, const VkAllocationCallbacks* pAllocator,
                   std::vector<ImageInfo>* image_info_out) override;

  bool CanPresentPendingImage() override { return true; }

  bool GetSize(uint32_t* width_out, uint32_t* height_out) override;

  VkResult GetPresentModes(VkPhysicalDevice physicalDevice,
                           VkLayerInstanceDispatchTable* dispatch_table, uint32_t* pCount,
                           VkPresentModeKHR* pPresentModes) override;

  void RemoveImage(uint32_t image_id) override;

  void PresentImage(uint32_t image_id, std::vector<std::unique_ptr<PlatformEvent>> acquire_fences,
                    std::vector<std::unique_ptr<PlatformEvent>> release_fences,
                    VkQueue queue) override;

  SupportedImageProperties& GetSupportedImageProperties() override;

 private:
  VkLayerDispatchTable* dispatch_table() {
    assert(device_layer_data_);
    return device_layer_data_->device_dispatch_table.get();
  }

  SupportedImageProperties supported_image_properties_;
  VkSurfaceKHR surface_;
  VkSwapchainKHR swapchain_;
  VkDevice device_{};
  LayerData* device_layer_data_;
  VkCommandPool command_pool_;
  std::vector<VkImage> dst_images_;
  std::vector<VkSemaphore> frame_acquire_semaphores_;
  std::vector<VkSemaphore> frame_present_semaphores_;
  std::vector<VkFence> frame_complete_fences_;
  std::vector<VkCommandBuffer> frame_command_buffers_;
  uint64_t frame_index_ = 0;
  bool is_protected_ = false;

  struct SrcImage {
    VkImage image;
    uint32_t width;
    uint32_t height;
    VkSemaphore acquire_semaphore;
    VkSemaphore release_semaphore;
  };
  std::map<uint32_t, SrcImage> src_image_map_;
};

}  // namespace image_pipe_swapchain

#endif  // SRC_LIB_VULKAN_SWAPCHAIN_SWAPCHAIN_COPY_SURFACE_H_
