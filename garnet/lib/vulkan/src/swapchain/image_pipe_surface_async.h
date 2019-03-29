// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_SRC_SWAPCHAIN_IMAGE_PIPE_SURFACE_ASYNC_H_
#define GARNET_LIB_VULKAN_SRC_SWAPCHAIN_IMAGE_PIPE_SURFACE_ASYNC_H_

#include "image_pipe_surface.h"

#include <lib/async-loop/cpp/loop.h>
#include <mutex>
#include <thread>

namespace image_pipe_swapchain {

// An implementation of ImagePipeSurface based on an async fidl ImagePipe.
class ImagePipeSurfaceAsync : public ImagePipeSurface {
 public:
  ImagePipeSurfaceAsync(zx_handle_t image_pipe_handle)
      : loop_(&kAsyncLoopConfigNoAttachToThread) {
    image_pipe_.Bind(zx::channel(image_pipe_handle), loop_.dispatcher());
    loop_.StartThread();
  }

  bool CreateImage(VkDevice device, VkLayerDispatchTable* pDisp,
                   VkFormat format, VkImageUsageFlags usage,
                   VkSwapchainCreateFlagsKHR swapchain_flags,
                   fuchsia::images::ImageInfo image_info, uint32_t image_count,
                   const VkAllocationCallbacks* pAllocator,
                   std::vector<ImageInfo>* image_info_out) override;

  void RemoveImage(uint32_t image_id) override;

  void PresentImage(uint32_t image_id, std::vector<zx::event> acquire_fences,
                    std::vector<zx::event> release_fences) override;

 private:
  void PresentNextImageLocked();

  async::Loop loop_;
  std::mutex mutex_;
  fuchsia::images::ImagePipePtr image_pipe_;
  struct PendingPresent {
    uint32_t image_id;
    std::vector<zx::event> acquire_fences;
    std::vector<zx::event> release_fences;
  };
  std::vector<PendingPresent> queue_;
  bool present_pending_ = false;
};

}  // namespace image_pipe_swapchain

#endif  // GARNET_LIB_VULKAN_SRC_SWAPCHAIN_IMAGE_PIPE_SURFACE_ASYNC_H_
