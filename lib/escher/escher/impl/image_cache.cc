// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/image_cache.h"

#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/gpu_allocator.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

ImageCache::ImageCache(vk::Device device,
                       vk::PhysicalDevice physical_device,
                       vk::Queue queue,
                       GpuAllocator* allocator,
                       CommandBufferPool* command_buffer_pool)
    : device_(device),
      physical_device_(physical_device),
      queue_(queue),
      allocator_(allocator),
      command_buffer_pool_(command_buffer_pool) {}

ImageCache::~ImageCache() {
  FTL_CHECK(image_count_ == 0);
}

ftl::RefPtr<Image> ImageCache::NewImage(const vk::ImageCreateInfo& info) {
  vk::Image image = ESCHER_CHECKED_VK_RESULT(device_.createImage(info));

  vk::MemoryRequirements reqs = device_.getImageMemoryRequirements(image);
  GpuMemPtr memory =
      allocator_->Allocate(reqs, vk::MemoryPropertyFlagBits::eDeviceLocal);

  vk::Result result =
      device_.bindImageMemory(image, memory->base(), memory->offset());
  FTL_CHECK(result == vk::Result::eSuccess);

  image_count_++;
  return AdoptRef(new Image(image, info.format, info.extent.width,
                            info.extent.height, std::move(memory), this));
}

ftl::RefPtr<Image> ImageCache::GetDepthImage(vk::Format format,
                                             uint32_t width,
                                             uint32_t height) {
  vk::ImageCreateInfo info;
  info.imageType = vk::ImageType::e2D;
  info.format = format;
  info.extent = vk::Extent3D{width, height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = vk::SampleCountFlagBits::e1;
  info.tiling = vk::ImageTiling::eOptimal;
  info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
  info.initialLayout = vk::ImageLayout::eUndefined;

  auto image = NewImage(info);
  TransitionImageLayout(image, vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eDepthStencilAttachmentOptimal);
  return image;
}

void ImageCache::TransitionImageLayout(const ImagePtr& image,
                                       vk::ImageLayout old_layout,
                                       vk::ImageLayout new_layout) {
  auto command_buffer = command_buffer_pool_->GetCommandBuffer(nullptr);

  vk::ImageMemoryBarrier barrier;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image->image();

  if (new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    if (image->HasStencilComponent()) {
      barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
    }
  } else {
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  }

  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  if (old_layout == vk::ImageLayout::ePreinitialized &&
      new_layout == vk::ImageLayout::eTransferSrcOptimal) {
    barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
  } else if (old_layout == vk::ImageLayout::ePreinitialized &&
             new_layout == vk::ImageLayout::eTransferDstOptimal) {
    barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
  } else if (old_layout == vk::ImageLayout::eTransferDstOptimal &&
             new_layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  } else if (old_layout == vk::ImageLayout::eUndefined &&
             new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
    barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                            vk::AccessFlagBits::eDepthStencilAttachmentWrite;
  } else {
    FTL_LOG(ERROR) << "Unsupported layout transition from: "
                   << to_string(old_layout) << " to: " << to_string(new_layout);
    FTL_DCHECK(false);
    return;
  }
  command_buffer->get().pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                        vk::PipelineStageFlagBits::eTopOfPipe,
                                        vk::DependencyFlags(), 0, nullptr, 0,
                                        nullptr, 1, &barrier);
  command_buffer->Submit(queue_);
}

void ImageCache::DestroyImage(vk::Image image, vk::Format format) {
  device_.destroyImage(image);
  image_count_--;
}

ImageCache::Image::Image(vk::Image image,
                         vk::Format format,
                         uint32_t width,
                         uint32_t height,
                         GpuMemPtr memory,
                         ImageCache* cache)
    : escher::Image(image, format, width, height),
      cache_(cache),
      memory_(std::move(memory)) {}

ImageCache::Image::~Image() {
  cache_->DestroyImage(image(), format());
}

}  // namespace impl
}  // namespace escher
