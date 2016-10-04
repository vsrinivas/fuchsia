// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/image_cache.h"

#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

Image::Image(ImageCache* cache, vk::Image image, vk::DeviceMemory memory)
    : cache_(cache), image_(image), memory_(memory) {}

Image::~Image() {
  cache_->DestroyImage(image_, memory_);
}

ImageCache::ImageCache(vk::Device device, vk::PhysicalDevice physical_device)
    : device_(device), physical_device_(physical_device) {}

ImageCache::~ImageCache() {
  FTL_CHECK(image_count_ == 0);
}

ftl::RefPtr<Image> ImageCache::GetImage(const vk::ImageCreateInfo& info) {
  vk::Image image = ESCHER_CHECKED_VK_RESULT(device_.createImage(info));

  vk::DeviceMemory memory;
  {
    vk::MemoryAllocateInfo info;
    vk::MemoryRequirements reqs = device_.getImageMemoryRequirements(image);
    info.allocationSize = reqs.size;
    info.memoryTypeIndex =
        GetMemoryTypeIndex(physical_device_, reqs.memoryTypeBits,
                           vk::MemoryPropertyFlagBits::eDeviceLocal);
    memory = ESCHER_CHECKED_VK_RESULT(device_.allocateMemory(info));
    vk::Result result = device_.bindImageMemory(image, memory, 0);
    FTL_CHECK(result == vk::Result::eSuccess);
  }

  image_count_++;
  return AdoptRef(new Image(this, image, memory));
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
  info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
               vk::ImageUsageFlagBits::eTransferSrc;
  info.initialLayout = vk::ImageLayout::eUndefined;
  return GetImage(info);
}

void ImageCache::DestroyImage(vk::Image image, vk::DeviceMemory memory) {
  device_.destroyImage(image);
  device_.freeMemory(memory);
  image_count_--;
}

}  // namespace impl
}  // namespace escher
