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
                       CommandBufferPool* main_pool,
                       CommandBufferPool* transfer_pool,
                       GpuAllocator* allocator)
    : device_(device),
      physical_device_(physical_device),
      main_queue_(main_pool->queue()),
      transfer_queue_(transfer_pool->queue()),
      main_command_buffer_pool_(main_pool),
      transfer_command_buffer_pool_(transfer_pool),
      allocator_(allocator) {}

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
  info.sharingMode = vk::SharingMode::eExclusive;

  auto image = NewImage(info, vk::MemoryPropertyFlagBits::eDeviceLocal);

  auto command_buffer = main_command_buffer_pool_->GetCommandBuffer();
  command_buffer->TransitionImageLayout(
      image, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);
  command_buffer->Submit(main_queue_, nullptr);

  return image;
}

ImagePtr ImageCache::NewRgbaImage(uint32_t width,
                                  uint32_t height,
                                  uint8_t* pixels) {
  // Create a command-buffer that will copy the pixels to the final image.
  // Do this first because it may free up memory that was used by previous
  // uploads (when finished command-buffers release any resources that they
  // were retaining).
  auto command_buffer = transfer_command_buffer_pool_->GetCommandBuffer();
  SemaphorePtr semaphore = Semaphore::New(device_);
  command_buffer->AddSignalSemaphore(semaphore);

  // Create the "transfer source" Image.
  vk::ImageCreateInfo info;
  info.imageType = vk::ImageType::e2D;
  info.format = vk::Format::eR8G8B8A8Unorm;
  info.extent = vk::Extent3D{width, height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = vk::SampleCountFlagBits::e1;
  info.tiling = vk::ImageTiling::eLinear;
  info.usage = vk::ImageUsageFlagBits::eTransferSrc;
  info.initialLayout = vk::ImageLayout::ePreinitialized;
  info.sharingMode = vk::SharingMode::eExclusive;
  // TODO: potential performance gains by not using eHostCoherent.  Probably
  // negligible.  Would involve flushing the data after unmapping it below.
  auto src_image =
      NewImage(info, vk::MemoryPropertyFlagBits::eHostVisible |
                         vk::MemoryPropertyFlagBits::eHostCoherent);

  // Create the image that will be returned from this function.
  info.tiling = vk::ImageTiling::eOptimal;
  info.usage =
      vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
  auto dst_image = NewImage(info, vk::MemoryPropertyFlagBits::eDeviceLocal);
  dst_image->SetWaitSemaphore(std::move(semaphore));

  // Copy the pixels into the "transfer source" image.
  uint8_t* mapped = src_image->Map();
  memcpy(mapped, pixels, width * height * 4);
  src_image->Unmap();

  // Write image-copy command, and submit the command buffer.  No barrier is
  // required since we have added a "wait semaphore" to dst_image.
  // TODO: if we weren't using a transfer-only queue, it would be more efficient
  // to use a barrier than a semaphore.
  vk::ImageSubresourceLayers subresource;
  subresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  subresource.baseArrayLayer = 0;
  subresource.mipLevel = 0;
  subresource.layerCount = 1;
  vk::ImageCopy region;
  region.srcSubresource = subresource;
  region.dstSubresource = subresource;
  region.srcOffset = vk::Offset3D{0, 0, 0};
  region.dstOffset = vk::Offset3D{0, 0, 0};
  region.extent.width = width;
  region.extent.height = height;
  region.extent.depth = 1;

  command_buffer->TransitionImageLayout(src_image,
                                        vk::ImageLayout::ePreinitialized,
                                        vk::ImageLayout::eTransferSrcOptimal);
  command_buffer->TransitionImageLayout(dst_image,
                                        vk::ImageLayout::ePreinitialized,
                                        vk::ImageLayout::eTransferDstOptimal);
  command_buffer->CopyImage(std::move(src_image), dst_image,
                            vk::ImageLayout::eTransferSrcOptimal,
                            vk::ImageLayout::eTransferDstOptimal, &region);
  command_buffer->TransitionImageLayout(
      dst_image, vk::ImageLayout::eTransferDstOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal);
  command_buffer->Submit(transfer_queue_, nullptr);

  return dst_image;
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

    // TODO: only flush if the coherent bit isn't set; also see Buffer::Unmap().
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
