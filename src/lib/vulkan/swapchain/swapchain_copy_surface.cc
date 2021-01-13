// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swapchain_copy_surface.h"

#include "vk_dispatch_table_helper.h"
#include "vulkan/vk_layer.h"

#define LOG_VERBOSE(msg, ...) \
  if (true)                   \
  fprintf(stderr, "%s:%d " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__)

namespace image_pipe_swapchain {

constexpr uint32_t kDstOffset = 0;

SwapchainCopySurface::SwapchainCopySurface() {
  supported_image_properties_.formats = {
      {VK_FORMAT_R8G8B8A8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR},
      {VK_FORMAT_R8G8B8A8_SRGB, VK_COLORSPACE_SRGB_NONLINEAR_KHR},
      {VK_FORMAT_B8G8R8A8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR},
      {VK_FORMAT_B8G8R8A8_SRGB, VK_COLORSPACE_SRGB_NONLINEAR_KHR}};
}

SupportedImageProperties& SwapchainCopySurface::GetSupportedImageProperties() {
  return supported_image_properties_;
}

bool SwapchainCopySurface::GetSize(uint32_t* width_out, uint32_t* height_out) { return false; }

VkResult SwapchainCopySurface::GetPresentModes(VkPhysicalDevice physicalDevice,
                                               VkLayerInstanceDispatchTable* dispatch_table,
                                               uint32_t* pCount, VkPresentModeKHR* pPresentModes) {
  return dispatch_table->GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface_, pCount,
                                                                 pPresentModes);
}

#if defined(VK_USE_PLATFORM_FUCHSIA)
bool SwapchainCopySurface::OnCreateSurface(VkInstance instance,
                                           VkLayerInstanceDispatchTable* dispatch_table,
                                           const VkImagePipeSurfaceCreateInfoFUCHSIA* pCreateInfo,
                                           const VkAllocationCallbacks* pAllocator) {
  VkResult result =
      dispatch_table->CreateImagePipeSurfaceFUCHSIA(instance, pCreateInfo, pAllocator, &surface_);

#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
bool SwapchainCopySurface::OnCreateSurface(VkInstance instance,
                                           VkLayerInstanceDispatchTable* dispatch_table,
                                           const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
                                           const VkAllocationCallbacks* pAllocator) {
  VkResult result =
      dispatch_table->CreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator, &surface_);

#endif

  if (result != VK_SUCCESS) {
    LOG_VERBOSE("CreateSurface failed: %d", result);
    return false;
  }

  return true;
}

void SwapchainCopySurface::OnDestroySurface(VkInstance instance,
                                            VkLayerInstanceDispatchTable* dispatch_table,
                                            const VkAllocationCallbacks* pAllocator) {
  dispatch_table->DestroySurfaceKHR(instance, surface_, pAllocator);
}

bool SwapchainCopySurface::OnCreateSwapchain(VkDevice device, LayerData* device_layer_data,
                                             const VkSwapchainCreateInfoKHR* pCreateInfo,
                                             const VkAllocationCallbacks* pAllocator) {
  device_ = device;
  device_layer_data_ = device_layer_data;
  frame_index_ = 0;
  is_protected_ = pCreateInfo->flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR;

  VkSwapchainCreateInfoKHR swapchain_create_info = *pCreateInfo;
  swapchain_create_info.surface = surface_;
  swapchain_create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  VkResult result =
      dispatch_table()->CreateSwapchainKHR(device, &swapchain_create_info, pAllocator, &swapchain_);
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("CreateSwapchainKHR failed: %d", result);
    return false;
  }

  uint32_t count;
  result = dispatch_table()->GetSwapchainImagesKHR(device, swapchain_, &count, nullptr);
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("GetSwapchainImagesKHR failed: %d", result);
    return false;
  }

  dst_images_.resize(count);
  frame_acquire_semaphores_.resize(count);
  frame_present_semaphores_.resize(count);
  frame_complete_fences_.resize(count);
  frame_command_buffers_.resize(count);

  result = dispatch_table()->GetSwapchainImagesKHR(device, swapchain_, &count, dst_images_.data());
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("GetSwapchainImagesKHR failed: %d", result);
    return false;
  }

  for (uint32_t i = 0; i < frame_acquire_semaphores_.size(); ++i) {
    VkSemaphoreCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};

    result = dispatch_table()->CreateSemaphore(device, &create_info, pAllocator,
                                               &frame_acquire_semaphores_[i]);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("CreateSemaphore failed: %d", result);
      return false;
    }
  }

  for (uint32_t i = 0; i < frame_present_semaphores_.size(); ++i) {
    VkSemaphoreCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};

    result = dispatch_table()->CreateSemaphore(device, &create_info, pAllocator,
                                               &frame_present_semaphores_[i]);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("CreateSemaphore failed: %d", result);
      return false;
    }
  }

  for (uint32_t i = 0; i < frame_complete_fences_.size(); ++i) {
    VkFenceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    result =
        dispatch_table()->CreateFence(device, &create_info, pAllocator, &frame_complete_fences_[i]);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("CreateFence failed: %d", result);
      return false;
    }
  }

  {
    uint32_t flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (is_protected_) {
      flags |= VK_COMMAND_POOL_CREATE_PROTECTED_BIT;
    }

    VkCommandPoolCreateInfo pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
        .queueFamilyIndex = 0,
    };

    VkResult result =
        dispatch_table()->CreateCommandPool(device_, &pool_create_info, pAllocator, &command_pool_);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("CreateCommandPool failed: %d", result);
      return false;
    }
  }

  VkCommandBufferAllocateInfo allocate_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = command_pool_,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = static_cast<uint32_t>(frame_command_buffers_.size())};

  result = dispatch_table()->AllocateCommandBuffers(device_, &allocate_info,
                                                    frame_command_buffers_.data());
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("AllocateCommandBuffers failed: %d", result);
    return false;
  }

  // Initialize dispatch objects
  for (auto& command_buffer : frame_command_buffers_) {
    assert(device_layer_data_->fpSetDeviceLoaderData);
    device_layer_data_->fpSetDeviceLoaderData(device_, command_buffer);
  }

  return true;
}

void SwapchainCopySurface::OnDestroySwapchain(VkDevice device,
                                              const VkAllocationCallbacks* pAllocator) {
  dispatch_table()->DestroySwapchainKHR(device, swapchain_, pAllocator);

  for (auto& pair : src_image_map_) {
    SrcImage& src_image = pair.second;
    dispatch_table()->DestroySemaphore(device, src_image.acquire_semaphore, pAllocator);
    dispatch_table()->DestroySemaphore(device, src_image.release_semaphore, pAllocator);
  }

  for (uint32_t i = 0; i < frame_acquire_semaphores_.size(); ++i) {
    dispatch_table()->DestroySemaphore(device, frame_acquire_semaphores_[i], pAllocator);
  }

  for (uint32_t i = 0; i < frame_present_semaphores_.size(); ++i) {
    dispatch_table()->DestroySemaphore(device, frame_present_semaphores_[i], pAllocator);
  }

  for (uint32_t i = 0; i < frame_complete_fences_.size(); ++i) {
    dispatch_table()->DestroyFence(device, frame_complete_fences_[i], pAllocator);
  }

  dispatch_table()->FreeCommandBuffers(device_, command_pool_,
                                       static_cast<uint32_t>(frame_command_buffers_.size()),
                                       frame_command_buffers_.data());
  dispatch_table()->DestroyCommandPool(device_, command_pool_, pAllocator);

  src_image_map_.clear();
  frame_acquire_semaphores_.clear();
  frame_present_semaphores_.clear();
  frame_complete_fences_.clear();
  frame_command_buffers_.clear();

  command_pool_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
  device_layer_data_ = nullptr;
}

bool SwapchainCopySurface::CreateImage(VkDevice device, VkLayerDispatchTable* dispatch_table,
                                       VkFormat format, VkImageUsageFlags usage,
                                       VkSwapchainCreateFlagsKHR swapchain_flags, VkExtent2D extent,
                                       uint32_t image_count,
                                       const VkAllocationCallbacks* pAllocator,
                                       std::vector<ImageInfo>* image_info_out) {
  uint32_t image_flags = 0;
  if (swapchain_flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR)
    image_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
  if (swapchain_flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR) {
    image_flags |= VK_IMAGE_CREATE_PROTECTED_BIT;
  }

  VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = image_flags,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = VkExtent3D{extent.width, extent.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,      // not used since not sharing
      .pQueueFamilyIndices = nullptr,  // not used since not sharing
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  for (uint32_t i = 0; i < image_count; ++i) {
    VkImage image;
    VkResult result = dispatch_table->CreateImage(device, &image_create_info, pAllocator, &image);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("CreateImage failed: %d", result);
      return false;
    }

    VkMemoryRequirements memory_requirements;
    dispatch_table->GetImageMemoryRequirements(device, image, &memory_requirements);

    VkMemoryAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = memory_requirements.size,
        // Find lowest usable index.
        .memoryTypeIndex = static_cast<uint32_t>(__builtin_ctz(memory_requirements.memoryTypeBits)),
    };

    VkDeviceMemory memory;
    result = dispatch_table->AllocateMemory(device, &alloc_info, pAllocator, &memory);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("AllocateMemory failed: %d", result);
      return result;
    }
    result = dispatch_table->BindImageMemory(device, image, memory, 0);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("BindImageMemory failed: %d", result);
      return result;
    }

    ImageInfo info = {.image = image, .memory = memory, .image_id = next_image_id()};
    image_info_out->push_back(info);

    SrcImage src_image = {.image = image, .width = extent.width, .height = extent.height};

    VkSemaphoreCreateInfo semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};

    result = dispatch_table->CreateSemaphore(device, &semaphore_create_info, pAllocator,
                                             &src_image.acquire_semaphore);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("CreateSemaphore failed: %d", result);
      return result;
    }

    result = dispatch_table->CreateSemaphore(device, &semaphore_create_info, pAllocator,
                                             &src_image.release_semaphore);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("CreateSemaphore failed: %d", result);
      return result;
    }

    src_image_map_[info.image_id] = src_image;
  }

  return true;
}

void SwapchainCopySurface::RemoveImage(uint32_t image_id) {
  // Empty
}

void SwapchainCopySurface::PresentImage(uint32_t image_id,
                                        std::vector<std::unique_ptr<PlatformEvent>> acquire_fences,
                                        std::vector<std::unique_ptr<PlatformEvent>> release_fences,
                                        VkQueue queue) {
  // We submit a command buffer to copy from the rendered image into the backend swapchain
  // image. The command buffer ignores acquire_fences because we're guaranteed ordering on the
  // queue, so we wait only on the backend swapchain acquire semaphore. The command buffer signals
  // the release_fences, as well as the backend present semaphore.
  std::vector<VkSemaphore> wait_semaphores;
  std::vector<VkSemaphore> signal_semaphores;

  uint32_t frame_index = static_cast<uint32_t>(frame_index_ % dst_images_.size());
  frame_index_ += 1;

  auto iter = src_image_map_.find(image_id);
  if (iter == src_image_map_.end()) {
    LOG_VERBOSE("Couldn't find image_id %u", image_id);
    return;
  }

  SrcImage& src_image = iter->second;

  constexpr uint64_t kTimeoutNs = UINT64_MAX;

  {
    // Wait to reuse frame resources
    VkResult result = dispatch_table()->WaitForFences(
        device_, 1, &frame_complete_fences_[frame_index], VK_TRUE, kTimeoutNs);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("WaitForFences failed: %d", result);
      return;
    }

    dispatch_table()->ResetFences(device_, 1, &frame_complete_fences_[frame_index]);
  }

  uint32_t dst_swap_index = 0;  // Used in acquire and present

  {
    VkResult result = dispatch_table()->AcquireNextImageKHR(device_, swapchain_, kTimeoutNs,
                                                            frame_acquire_semaphores_[frame_index],
                                                            VK_NULL_HANDLE,  // fence
                                                            &dst_swap_index);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("AcquireNextImageKHR failed: %d", result);
      return;
    }

    wait_semaphores.push_back(frame_acquire_semaphores_[frame_index]);
  }

  {
    // Import src release semaphore
    assert(release_fences.size() == 1);

    VkResult result = release_fences[0]->ImportToSemaphore(device_, dispatch_table(),
                                                           src_image.release_semaphore);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("ImportToSemaphore failed: %d", result);
      return;
    }

    signal_semaphores.push_back(src_image.release_semaphore);
  }

  signal_semaphores.push_back(frame_present_semaphores_[frame_index]);

  {
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };

    VkResult result =
        dispatch_table()->BeginCommandBuffer(frame_command_buffers_[frame_index], &begin_info);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("BeginCommandBuffer failed: %d", result);
      return;
    }
  }

  {
    // Transition src image to transfer layout
    VkImageMemoryBarrier image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = src_image.image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};

    dispatch_table()->CmdPipelineBarrier(frame_command_buffers_[frame_index],
                                         VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,  // srcStageMask
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,      // dstStageMask
                                         0,                                   // dependencyFlags
                                         0,                                   // memoryBarrierCount
                                         nullptr,                             // pMemoryBarriers
                                         0,        // bufferMemoryBarrierCount
                                         nullptr,  // pBufferMemoryBarriers
                                         1,        // imageMemoryBarrierCount
                                         &image_barrier);
  }

  {
    // Transition dst image to transfer layout
    VkImageMemoryBarrier image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst_images_[dst_swap_index],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};

    dispatch_table()->CmdPipelineBarrier(frame_command_buffers_[frame_index],
                                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,  // srcStageMask
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,     // dstStageMask
                                         0,                                  // dependencyFlags
                                         0,                                  // memoryBarrierCount
                                         nullptr,                            // pMemoryBarriers
                                         0,        // bufferMemoryBarrierCount
                                         nullptr,  // pBufferMemoryBarriers
                                         1,        // imageMemoryBarrierCount
                                         &image_barrier);
  }

  {
    VkImageCopy region = {.srcSubresource =
                              {
                                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .mipLevel = 0,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1,
                              },
                          .srcOffset = {0, 0, 0},
                          .dstSubresource =
                              {
                                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .mipLevel = 0,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1,
                              },
                          .dstOffset = {kDstOffset, kDstOffset, 0},
                          .extent = {src_image.width, src_image.height, 0}};

    dispatch_table()->CmdCopyImage(frame_command_buffers_[frame_index], src_image.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,  // srcImageLayout
                                   dst_images_[dst_swap_index],
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,  // dstImageLayout
                                   1,                                     // regionCount
                                   &region);
  }

  {
    // Transition dst image to present layout
    VkImageMemoryBarrier image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst_images_[dst_swap_index],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};

    dispatch_table()->CmdPipelineBarrier(frame_command_buffers_[frame_index],
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,        // srcStageMask
                                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,  // dstStageMask
                                         0,                                     // dependencyFlags
                                         0,        // memoryBarrierCount
                                         nullptr,  // pMemoryBarriers
                                         0,        // bufferMemoryBarrierCount
                                         nullptr,  // pBufferMemoryBarriers
                                         1,        // imageMemoryBarrierCount
                                         &image_barrier);
  }

  {
    // Transition src image back to present layout
    VkImageMemoryBarrier image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst_images_[dst_swap_index],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};

    dispatch_table()->CmdPipelineBarrier(frame_command_buffers_[frame_index],
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,        // srcStageMask
                                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,  // dstStageMask
                                         0,                                     // dependencyFlags
                                         0,        // memoryBarrierCount
                                         nullptr,  // pMemoryBarriers
                                         0,        // bufferMemoryBarrierCount
                                         nullptr,  // pBufferMemoryBarriers
                                         1,        // imageMemoryBarrierCount
                                         &image_barrier);
  }

  VkResult result = dispatch_table()->EndCommandBuffer(frame_command_buffers_[frame_index]);
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("EndCommandBuffer failed: %d", result);
    return;
  }

  {
    VkProtectedSubmitInfo protected_submit_info = {
        .sType = VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO,
        .pNext = nullptr,
        .protectedSubmit = VK_TRUE,
    };
    std::vector<VkPipelineStageFlags> flag_bits(wait_semaphores.size(),
                                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = is_protected_ ? &protected_submit_info : nullptr,
        .waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size()),
        .pWaitSemaphores = wait_semaphores.data(),
        .pWaitDstStageMask = flag_bits.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &frame_command_buffers_[frame_index],
        .signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size()),
        .pSignalSemaphores = signal_semaphores.data()};

    VkResult result =
        dispatch_table()->QueueSubmit(queue, 1, &submit_info, frame_complete_fences_[frame_index]);
    if (result != VK_SUCCESS) {
      LOG_VERBOSE("QueueSubmit failed: %d", result);
      return;
    }
  }

  {
    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame_present_semaphores_[frame_index],
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &dst_swap_index,
        .pResults = nullptr,
    };

    dispatch_table()->QueuePresentKHR(queue, &present_info);
  }
}

}  // namespace image_pipe_swapchain
