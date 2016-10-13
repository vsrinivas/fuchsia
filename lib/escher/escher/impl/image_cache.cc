// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/image_cache.h"

#include "escher/impl/gpu_allocator.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

ImageCache::ImageCache(vk::Device device,
                       vk::PhysicalDevice physical_device,
                       GpuAllocator* allocator)
    : device_(device),
      physical_device_(physical_device),
      allocator_(allocator) {}

ImageCache::~ImageCache() {
  FTL_CHECK(image_count_ == 0);
}

ftl::RefPtr<Image> ImageCache::NewImage(const vk::ImageCreateInfo& info) {
  vk::Image image = ESCHER_CHECKED_VK_RESULT(device_.createImage(info));

  vk::MemoryRequirements reqs = device_.getImageMemoryRequirements(image);
  GpuMem memory =
      allocator_->Allocate(reqs, vk::MemoryPropertyFlagBits::eDeviceLocal);

  vk::Result result =
      device_.bindImageMemory(image, memory.base(), memory.offset());
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
  info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
               vk::ImageUsageFlagBits::eTransferSrc;
  info.initialLayout = vk::ImageLayout::eUndefined;
  return NewImage(info);
}

void ImageCache::DestroyImage(vk::Image image,
                              vk::Format format,
                              GpuMem memory) {
  device_.destroyImage(image);
  image_count_--;
}

ImageCache::Image::Image(vk::Image image,
                         vk::Format format,
                         uint32_t width,
                         uint32_t height,
                         GpuMem memory,
                         ImageCache* cache)
    : escher::Image(image, format, width, height),
      cache_(cache),
      memory_(std::move(memory)) {}

ImageCache::Image::~Image() {
  cache_->DestroyImage(image(), format(), std::move(memory_));
}

}  // namespace impl
}  // namespace escher
