// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/image_cache.h"

#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/gpu_uploader.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/util/image_loader.h"
#include "escher/vk/gpu_allocator.h"

namespace escher {
namespace impl {

ImageCache::ImageCache(const VulkanContext& context,
                       CommandBufferPool* pool,
                       GpuAllocator* allocator,
                       GpuUploader* uploader)
    : ImageOwner(context),
      queue_(pool->queue()),
      allocator_(allocator),
      uploader_(uploader) {}

ImageCache::~ImageCache() {}

ImagePtr ImageCache::NewImage(const ImageInfo& info) {
  if (ImagePtr result = FindImage(info)) {
    return result;
  }

  // Create a new vk::Image, since we couldn't find a suitable one.
  vk::ImageCreateInfo create_info;
  create_info.imageType = vk::ImageType::e2D;
  create_info.format = info.format;
  create_info.extent = vk::Extent3D{info.width, info.height, 1};
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  switch (info.sample_count) {
    case 1:
      create_info.samples = vk::SampleCountFlagBits::e1;
      break;
    case 2:
      create_info.samples = vk::SampleCountFlagBits::e2;
      break;
    case 4:
      create_info.samples = vk::SampleCountFlagBits::e4;
      break;
    case 8:
      create_info.samples = vk::SampleCountFlagBits::e8;
      break;
    default:
      FTL_DCHECK(false);
  }
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = info.usage;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;
  vk::Image image = ESCHER_CHECKED_VK_RESULT(device().createImage(create_info));

  // Allocate memory and bind it to the image.
  vk::MemoryRequirements reqs = device().getImageMemoryRequirements(image);
  GpuMemPtr memory = allocator_->Allocate(reqs, info.memory_flags);
  vk::Result result =
      device().bindImageMemory(image, memory->base(), memory->offset());
  FTL_CHECK(result == vk::Result::eSuccess);

  return CreateImage(
      std::make_unique<ImageCore>(this, info, image, std::move(memory)));
}

ImagePtr ImageCache::NewDepthImage(vk::Format format,
                                   uint32_t width,
                                   uint32_t height,
                                   vk::ImageUsageFlags additional_flags) {
  ImageInfo info;
  info.format = format;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage =
      additional_flags | vk::ImageUsageFlagBits::eDepthStencilAttachment;

  return NewImage(info);
}

ImagePtr ImageCache::NewColorAttachmentImage(
    uint32_t width,
    uint32_t height,
    vk::ImageUsageFlags additional_flags) {
  ImageInfo info;
  info.format = vk::Format::eB8G8R8A8Unorm;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage = additional_flags | vk::ImageUsageFlagBits::eColorAttachment;

  return NewImage(info);
}

ImagePtr ImageCache::NewImageFromPixels(vk::Format format,
                                        uint32_t width,
                                        uint32_t height,
                                        uint8_t* pixels,
                                        vk::ImageUsageFlags additional_flags) {
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

  ImageInfo info;
  info.format = format;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage = additional_flags | vk::ImageUsageFlagBits::eTransferDst |
               vk::ImageUsageFlagBits::eSampled;

  // Create the new image.
  auto image = NewImage(info);

  vk::BufferImageCopy region;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = width;
  region.imageExtent.height = height;
  region.imageExtent.depth = 1;
  region.bufferOffset = 0;

  writer.WriteImage(image, region, Semaphore::New(device()));
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

ImagePtr ImageCache::NewNoiseImage(uint32_t width,
                                   uint32_t height,
                                   vk::ImageUsageFlags additional_flags) {
  auto pixels = NewNoisePixels(width, height);
  return NewImageFromPixels(vk::Format::eR8Unorm, width, height, pixels.get(),
                            additional_flags);
}

ImagePtr ImageCache::FindImage(const ImageInfo& info) {
  auto& queue = unused_images_[info];
  if (queue.empty()) {
    return ImagePtr();
  } else {
    ImagePtr result = CreateImage(std::move(queue.front()));
    queue.pop();
    return result;
  }
}

void ImageCache::ReceiveResourceCore(std::unique_ptr<ResourceCore> core) {
  std::unique_ptr<ImageCore> image_core(
      static_cast<ImageCore*>(core.release()));
  auto& queue = unused_images_[image_core->info()];
  queue.push(std::move(image_core));
}

}  // namespace impl
}  // namespace escher
