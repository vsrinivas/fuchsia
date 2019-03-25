// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "image_pipe_surface_async.h"
#include "vk_dispatch_table_helper.h"
#include "vulkan/vk_layer.h"

namespace image_pipe_swapchain {

bool ImagePipeSurfaceAsync::CreateImage(
    VkDevice device, VkLayerDispatchTable* pDisp, VkFormat format,
    VkImageUsageFlags usage, fuchsia::images::ImageInfo image_info,
    uint32_t image_count, const VkAllocationCallbacks* pAllocator,
    std::vector<ImageInfo>* image_info_out) {
  for (uint32_t i = 0; i < image_count; ++i) {
    // Allocate a buffer.
    uint32_t width = image_info.width;
    uint32_t height = image_info.height;
    VkImageCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = width, .height = height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage image;
    VkResult result =
        pDisp->CreateImage(device, &create_info, pAllocator, &image);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "VkCreateImage failed: %d", result);
      return false;
    }

    VkMemoryRequirements memory_requirements;
    pDisp->GetImageMemoryRequirements(device, image, &memory_requirements);

    VkExportMemoryAllocateInfoKHR export_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR,
        .pNext = nullptr,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_FUCHSIA_VMO_BIT_KHR};

    VkMemoryAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &export_allocate_info,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory memory;
    result = pDisp->AllocateMemory(device, &alloc_info, pAllocator, &memory);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "vkAllocMemory failed: %d", result);
      return false;
    }
    result = pDisp->BindImageMemory(device, image, memory, 0);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "vkBindImageMemory failed: %d", result);
      return false;
    }
    zx::vmo vmo;
    // Export the vkDeviceMemory to a VMO.
    VkMemoryGetZirconHandleInfoFUCHSIA get_handle_info = {
        VK_STRUCTURE_TYPE_TEMP_MEMORY_GET_ZIRCON_HANDLE_INFO_FUCHSIA, nullptr,
        memory, VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA};

    result = pDisp->GetMemoryZirconHandleFUCHSIA(device, &get_handle_info,
                                                 vmo.reset_and_get_address());
    if (result != VK_SUCCESS) {
      fprintf(stderr, "GetMemoryZirconHandleFUCHSIA failed: %d", result);
      return false;
    }

    ImageInfo info = {
        .image = image,
        .memory = memory,
        .image_id = next_image_id(),
    };
    image_info_out->push_back(info);
    std::lock_guard<std::mutex> lock(mutex_);
    image_pipe_->AddImage(info.image_id, std::move(image_info), std::move(vmo),
                          0, memory_requirements.size,
                          fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  }
  return true;
}

void ImagePipeSurfaceAsync::RemoveImage(uint32_t image_id) {
  std::unique_lock<std::mutex> lock(mutex_);
  for (auto iter = queue_.begin(); iter != queue_.end();) {
    if (iter->image_id == image_id) {
      iter = queue_.erase(iter);
    } else {
      iter++;
    }
  }
  // TODO(SCN-1107) - remove this workaround
  static constexpr bool kUseWorkaround = true;
  while (kUseWorkaround && present_pending_) {
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    lock.lock();
  }
  image_pipe_->RemoveImage(image_id);
}

void ImagePipeSurfaceAsync::PresentImage(
    uint32_t image_id, std::vector<zx::event> acquire_fences,
    std::vector<zx::event> release_fences) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push_back(
      {image_id, std::move(acquire_fences), std::move(release_fences)});
  if (!present_pending_) {
    PresentNextImageLocked();
  }
}

void ImagePipeSurfaceAsync::PresentNextImageLocked() {
  assert(!present_pending_);

  if (queue_.empty())
    return;

  // To guarantee FIFO mode, we can't have Scenic drop any of our frames.
  // We accomplish that sending the next one only when we receive the callback
  // for the previous one.  We don't use the presentation info timing
  // parameters because we really just want to push out the next image asap.
  uint64_t presentation_time = zx_clock_get_monotonic();

  auto& present = queue_.front();
  image_pipe_->PresentImage(present.image_id, presentation_time,
                            std::move(present.acquire_fences),
                            std::move(present.release_fences),
                            // This callback happening in a separate thread.
                            [this](fuchsia::images::PresentationInfo pinfo) {
                              std::lock_guard<std::mutex> lock(mutex_);
                              present_pending_ = false;
                              PresentNextImageLocked();
                            });

  queue_.erase(queue_.begin());
  present_pending_ = true;
}

}  // namespace image_pipe_swapchain
