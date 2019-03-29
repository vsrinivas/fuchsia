// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_SRC_SWAPCHAIN_IMAGE_PIPE_SURFACE_DISPLAY_H_
#define GARNET_LIB_VULKAN_SRC_SWAPCHAIN_IMAGE_PIPE_SURFACE_DISPLAY_H_

#include <map>
#include "image_pipe_surface.h"

#include <lib/async-loop/cpp/loop.h>
#include "fuchsia/hardware/display/cpp/fidl.h"
#include "fuchsia/sysmem/cpp/fidl.h"

namespace image_pipe_swapchain {

// An implementation of ImagePipeSurface based on the display and sysmem APIs.
class ImagePipeSurfaceDisplay : public ImagePipeSurface {
 public:
  ImagePipeSurfaceDisplay();
  ~ImagePipeSurfaceDisplay() override;

  bool Init() override;

  bool CreateImage(VkDevice device, VkLayerDispatchTable* pDisp,
                   VkFormat format, VkImageUsageFlags usage,
                   VkSwapchainCreateFlagsKHR swapchain_flags,
                   fuchsia::images::ImageInfo image_info, uint32_t image_count,
                   const VkAllocationCallbacks* pAllocator,
                   std::vector<ImageInfo>* image_info_out) override;

  bool CanPresentPendingImage() override { return false; }

  bool GetSize(uint32_t* width_out, uint32_t* height_out) override;

  void RemoveImage(uint32_t image_id) override;

  void PresentImage(uint32_t image_id, std::vector<zx::event> acquire_fences,
                    std::vector<zx::event> release_fences) override;

 private:
  void ControllerError(zx_status_t status);
  void ControllerDisplaysChanged(std::vector<fuchsia::hardware::display::Info>,
                                 std::vector<uint64_t>);

  bool WaitForAsyncMessage();

  // This loop is manually pumped in method calls and doesn't have its own
  // thread.
  async::Loop loop_;
  std::map<uint64_t, uint64_t> image_id_map;

  int dc_fd_ = -1;
  bool display_connection_exited_ = false;
  bool got_message_response_ = false;
  bool have_display_ = false;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint64_t display_id_ = 0;
  uint64_t layer_id_ = 0;
  fuchsia::hardware::display::ControllerPtr display_controller_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

}  // namespace image_pipe_swapchain

#endif  // GARNET_LIB_VULKAN_SRC_SWAPCHAIN_IMAGE_PIPE_SURFACE_DISPLAY_H_
