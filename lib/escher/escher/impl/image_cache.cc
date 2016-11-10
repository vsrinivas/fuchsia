// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/image_cache.h"

#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/gpu_allocator.h"
#include "escher/impl/gpu_uploader.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/util/image_loader.h"

namespace escher {
namespace impl {

ImageCache::ImageCache(vk::Device device,
                       vk::PhysicalDevice physical_device,
                       CommandBufferPool* pool,
                       GpuAllocator* allocator,
                       GpuUploader* uploader)
    : device_(device),
      physical_device_(physical_device),
      queue_(pool->queue()),
      command_buffer_pool_(pool),
      allocator_(allocator),
      uploader_(uploader) {}

ImageCache::~ImageCache() {
  FTL_CHECK(image_count_ == 0);
}

ImagePtr ImageCache::NewImage(const vk::ImageCreateInfo& info,
                              vk::MemoryPropertyFlags memory_flags) {
  vk::Image image = ESCHER_CHECKED_VK_RESULT(device_.createImage(info));

  vk::MemoryRequirements reqs = device_.getImageMemoryRequirements(image);
  GpuMemPtr memory = allocator_->Allocate(reqs, memory_flags);

  vk::Result result =
      device_.bindImageMemory(image, memory->base(), memory->offset());
  FTL_CHECK(result == vk::Result::eSuccess);

  image_count_++;
  return AdoptRef(new Image(image, info.format, info.extent.width,
                            info.extent.height, std::move(memory), this));
}

ImagePtr ImageCache::GetDepthImage(vk::Format format,
                                   uint32_t width,
                                   uint32_t height,
                                   vk::ImageUsageFlags additional_flags) {
  vk::ImageCreateInfo info;
  info.imageType = vk::ImageType::e2D;
  info.format = format;
  info.extent = vk::Extent3D{width, height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = vk::SampleCountFlagBits::e1;
  info.tiling = vk::ImageTiling::eOptimal;
  info.usage =
      additional_flags | vk::ImageUsageFlagBits::eDepthStencilAttachment;
  info.initialLayout = vk::ImageLayout::eUndefined;
  info.sharingMode = vk::SharingMode::eExclusive;

  auto image = NewImage(info, vk::MemoryPropertyFlagBits::eDeviceLocal);

  auto command_buffer = command_buffer_pool_->GetCommandBuffer();
  command_buffer->TransitionImageLayout(
      image, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);
  command_buffer->Submit(queue_, nullptr);

  return image;
}

ImagePtr ImageCache::NewColorAttachmentImage(
    uint32_t width,
    uint32_t height,
    vk::ImageUsageFlags additional_flags) {
  vk::ImageCreateInfo info;
  info.imageType = vk::ImageType::e2D;
  info.format = vk::Format::eB8G8R8A8Srgb;
  info.extent = vk::Extent3D{width, height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = vk::SampleCountFlagBits::e1;
  info.tiling = vk::ImageTiling::eOptimal;
  info.usage = additional_flags | vk::ImageUsageFlagBits::eColorAttachment;
  info.initialLayout = vk::ImageLayout::eUndefined;
  info.sharingMode = vk::SharingMode::eExclusive;

  auto image = NewImage(info, vk::MemoryPropertyFlagBits::eDeviceLocal);

  auto command_buffer = command_buffer_pool_->GetCommandBuffer();
  command_buffer->TransitionImageLayout(
      image, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal);
  command_buffer->Submit(queue_, nullptr);

  return image;
}

ImagePtr ImageCache::NewImageFromPixels(vk::Format format,
                                        uint32_t width,
                                        uint32_t height,
                                        uint8_t* pixels) {
  size_t bytes_per_pixel = 0;
  switch (format) {
    case vk::Format::eR8G8B8A8Unorm:
      bytes_per_pixel = 4;
      break;
    case vk::Format::eR8Unorm:
      bytes_per_pixel = 1;
      break;
    default:
      FTL_CHECK(false);
  }

  auto writer = uploader_->GetWriter(width * height * bytes_per_pixel);
  memcpy(writer.ptr(), pixels, width * height * bytes_per_pixel);

  // Create the new image.
  vk::ImageCreateInfo info;
  info.imageType = vk::ImageType::e2D;
  info.format = format;
  info.extent = vk::Extent3D{width, height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = vk::SampleCountFlagBits::e1;
  info.tiling = vk::ImageTiling::eOptimal;
  info.usage =
      vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
  info.initialLayout = vk::ImageLayout::eUndefined;
  info.sharingMode = vk::SharingMode::eExclusive;
  auto image = NewImage(info, vk::MemoryPropertyFlagBits::eDeviceLocal);

  vk::BufferImageCopy region;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = width;
  region.imageExtent.height = height;
  region.imageExtent.depth = 1;
  region.bufferOffset = 0;

  writer.WriteImage(image, region, Semaphore::New(device_));
  writer.Submit();

  return image;
}

ImagePtr ImageCache::NewRgbaImage(uint32_t width,
                                  uint32_t height,
                                  uint8_t* pixels) {
  return NewImageFromPixels(vk::Format::eR8G8B8A8Unorm, width, height, pixels);
}

ImagePtr ImageCache::NewCheckerboardImage(uint32_t width, uint32_t height) {
  auto pixels = NewCheckerboardPixels(width, height);
  return NewImageFromPixels(vk::Format::eR8G8B8A8Unorm, width, height,
                            pixels.get());
}

ImagePtr ImageCache::NewNoiseImage(uint32_t width, uint32_t height) {
  auto pixels = NewNoisePixels(width, height);
  return NewImageFromPixels(vk::Format::eR8Unorm, width, height, pixels.get());
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
      mem_(std::move(memory)) {}

ImageCache::Image::~Image() {
  FTL_DCHECK(!mapped_);
  cache_->DestroyImage(get(), format());
}

uint8_t* ImageCache::Image::Map() {
  if (!mapped_) {
    mapped_ = ESCHER_CHECKED_VK_RESULT(
        cache_->device_.mapMemory(mem_->base(), mem_->offset(), mem_->size()));
  }
  return reinterpret_cast<uint8_t*>(mapped_);
}

void ImageCache::Image::Unmap() {
  if (mapped_) {
    vk::Device device = cache_->device_;

    // TODO: only flush if the coherent bit isn't set; also see
    // Buffer::Unmap().
    vk::MappedMemoryRange range;
    range.memory = mem_->base();
    range.offset = mem_->offset();
    range.size = mem_->size();
    device.flushMappedMemoryRanges(1, &range);

    device.unmapMemory(mem_->base());
    mapped_ = nullptr;
  }
}

}  // namespace impl
}  // namespace escher
