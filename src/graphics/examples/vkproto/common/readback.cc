// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkproto/common/readback.h"

#include <cassert>

#include "vulkan/vulkan.hpp"

namespace vkp {

std::optional<vk::UniqueDeviceMemory> TransitionToHostVisibleImage(
    const vk::PhysicalDevice& physical_device, const vk::Device& device, const vk::Image& src_image,
    const vk::Extent2D& extent, const vk::CommandPool& command_pool, const vk::Queue& queue,
    vk::SubresourceLayout* host_image_layout) {
  // Create the linear tiled, host visible destination image to copy to and
  // to read the memory from.
  vk::ImageCreateInfo host_image_info;
  host_image_info.imageType = vk::ImageType::e2D;
  host_image_info.format = vk::Format::eR8G8B8A8Unorm;
  host_image_info.extent = vk::Extent3D{extent.width, extent.height, 1};
  host_image_info.arrayLayers = 1;
  host_image_info.mipLevels = 1;
  host_image_info.initialLayout = vk::ImageLayout::eUndefined;
  host_image_info.samples = vk::SampleCountFlagBits::e1;
  host_image_info.tiling = vk::ImageTiling::eLinear;
  host_image_info.usage = vk::ImageUsageFlagBits::eTransferDst;

  std::optional<vk::UniqueDeviceMemory> empty;

  // Create the host visible destination image.
  auto [r_host_image, host_image] = device.createImageUnique(host_image_info);
  RTN_IF_VKH_ERR(empty, r_host_image, "Failed to create host visible readback image.\n");

  // Create backing memory for the host image.
  auto image_memory_requirements = device.getImageMemoryRequirements(*host_image);
  vk::MemoryAllocateInfo alloc_info;
  alloc_info.allocationSize = image_memory_requirements.size;

  // Memory must be host visible to map and copy from.
  RTN_IF_VKH_ERR(
      empty,
      vkp::FindMemoryIndex(physical_device, image_memory_requirements.memoryTypeBits,
                           vk::MemoryPropertyFlagBits::eHostVisible, &alloc_info.memoryTypeIndex),
      "Failed to find matching memory index.");

  auto r_host_image_memory = device.allocateMemoryUnique(alloc_info);
  RTN_IF_VKH_ERR(empty, r_host_image_memory.result,
                 "Failed to allocate memory for host visible image.\n");
  vk::UniqueDeviceMemory host_image_memory = std::move(r_host_image_memory.value);
  RTN_IF_VKH_ERR(empty, device.bindImageMemory(*host_image, *host_image_memory, 0),
                 "Failed to bind device memory to host visible image.\n");

  // Configure and submit a command buffer to copy from the offscreen color
  // attachment image to our host-visible destination image.
  vk::CommandBufferAllocateInfo cmd_buf_alloc_info;
  cmd_buf_alloc_info.setCommandBufferCount(1);
  cmd_buf_alloc_info.setCommandPool(command_pool);
  auto [r_cmd_bufs, command_buffers] = device.allocateCommandBuffersUnique(cmd_buf_alloc_info);
  RTN_IF_VKH_ERR(empty, r_cmd_bufs, "Failed to allocate command buffers.\n");
  const vk::UniqueCommandBuffer& command_buffer = command_buffers[0];

  vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
  RTN_IF_VKH_ERR(empty, command_buffer->begin(&begin_info), "Failed to begin command buffer.\n");

  // Transition destination image to transfer destination layout.
  const vk::ImageSubresourceRange subresource_range = {vk::ImageAspectFlagBits::eColor,
                                                       0 /* baseMipLevel */, 1 /* levelCount */,
                                                       0 /* baseArrayLayer */, 1 /* layerCount */};
  vk::ImageSubresource subresource = {subresource_range.aspectMask, subresource_range.baseMipLevel,
                                      subresource_range.baseArrayLayer};

  vk::ImageMemoryBarrier transfer_memory_barrier;
  transfer_memory_barrier.srcAccessMask = vk::AccessFlags{};
  transfer_memory_barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
  transfer_memory_barrier.oldLayout = vk::ImageLayout::eUndefined;
  transfer_memory_barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
  transfer_memory_barrier.image = *host_image;
  transfer_memory_barrier.subresourceRange = subresource_range;

  command_buffer->pipelineBarrier(
      vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
      vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
      0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
      1 /* imageMemoryBarrierCount */, &transfer_memory_barrier);

  vk::ImageCopy image_copy_region;
  image_copy_region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  image_copy_region.srcSubresource.layerCount = 1;
  image_copy_region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  image_copy_region.dstSubresource.layerCount = 1;
  image_copy_region.setExtent(vk::Extent3D{extent.width, extent.height, 1});

  command_buffer->copyImage(src_image, vk::ImageLayout::eTransferSrcOptimal, *host_image,
                            vk::ImageLayout::eTransferDstOptimal, 1, &image_copy_region);

  // Transition destination image to general layout, which is the required
  // layout for mapping the image memory later on.
  vk::ImageMemoryBarrier map_memory_barrier;
  map_memory_barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  map_memory_barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
  map_memory_barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
  map_memory_barrier.newLayout = vk::ImageLayout::eGeneral;
  map_memory_barrier.image = *host_image;
  map_memory_barrier.subresourceRange = subresource_range;

  command_buffer->pipelineBarrier(
      vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
      vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
      0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
      1 /* imageMemoryBarrierCount */, &map_memory_barrier);

  RTN_IF_VKH_ERR(empty, command_buffer->end(), "end failed\n");

  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &(*command_buffer);

  const vk::FenceCreateInfo fence_info(vk::FenceCreateFlagBits::eSignaled);
  auto [r_fence, unique_fence] = device.createFenceUnique(fence_info);
  RTN_IF_VKH_ERR(empty, r_fence, "Failed to create readback image transition fence.\n");
  vk::Fence& fence = *unique_fence;
  RTN_IF_VKH_ERR(empty, device.resetFences(1, &fence), "resetFences failed\n");

  RTN_IF_VKH_ERR(empty, queue.submit(1, &submit_info, fence),
                 "Failed to submit command buffer for readback image transition.\n");

  RTN_IF_VKH_ERR(empty,
                 device.waitForFences(1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
                 "waitForFences failed\n");

  device.getImageSubresourceLayout(*host_image, &subresource, host_image_layout);
  return std::make_optional<vk::UniqueDeviceMemory>(std::move(host_image_memory));
}

bool ReadPixels(const vk::PhysicalDevice& physical_device, const vk::Device& device,
                const vk::Image& src_image, const vk::Extent2D& src_image_size,
                const vk::CommandPool& command_pool, const vk::Queue& queue,
                const vk::Extent2D& size, const vk::Offset2D& offset,
                std::vector<uint32_t>* pixels) {
  assert(offset.x + size.width <= src_image_size.width &&
         offset.y + size.height <= src_image_size.height &&
         "Incompatible output buffer size vs requested size.");
  if (pixels->size() < size.width * size.height) {
    pixels->resize(size.width * size.height);
  }
  // Transition image.
  vk::SubresourceLayout host_image_layout;
  auto host_image_memory_opt = TransitionToHostVisibleImage(
      physical_device, device, src_image, src_image_size, command_pool, queue, &host_image_layout);
  RTN_IF_MSG(false, !host_image_memory_opt, "Unable to transition to host image for readback.\n");
  vk::UniqueDeviceMemory host_image_memory = std::move(host_image_memory_opt.value());

  // Map host image.
  auto [r_mapped_memory, mapped_memory] =
      device.mapMemory(*host_image_memory, 0 /* offset */, VK_WHOLE_SIZE, vk::MemoryMapFlags{});
  RTN_IF_VKH_ERR(false, r_mapped_memory, "Readback vulkan memory map failed.\n");

  vk::MappedMemoryRange range(*host_image_memory, 0 /* offset */, VK_WHOLE_SIZE);
  RTN_IF_VKH_ERR(false, device.invalidateMappedMemoryRanges(1 /* memoryRangeCount */, &range),
                 "invalidateMappedMemoryRanges failed\n");

  // Copy to output buffer.
  uint8_t* host_image_buffer = static_cast<uint8_t*>(mapped_memory);
  for (uint32_t y = 0; y < size.height; y++) {
    for (uint32_t x = 0; x < size.width; x++) {
      pixels->at(y * size.width + x) =
          *((uint32_t*)(host_image_buffer + ((offset.y + y) * host_image_layout.rowPitch) +
                        ((offset.x + x) * 4)));
    }
  }
  device.unmapMemory(*host_image_memory);

  return true;
}

}  // namespace vkp
