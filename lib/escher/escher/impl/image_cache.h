// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

#include "escher/forward_declarations.h"
#include "escher/impl/gpu_mem.h"
#include "escher/renderer/image.h"
#include "escher/renderer/image_owner.h"
#include "escher/util/hash.h"
#include "ftl/macros.h"
#include "ftl/memory/ref_counted.h"

namespace escher {
namespace impl {

class CommandBufferPool;
class GpuUploader;

// Allow client to obtain new or recycled Images.  All Images obtained from an
// ImageCache must be destroyed before the ImageCache is destroyed.
class ImageCache : public ImageOwner {
 public:
  // The allocator is used to allocate memory for newly-created images.  The
  // queue and CommandBufferPool are used to schedule image layout transitions.
  ImageCache(vk::Device device,
             vk::PhysicalDevice physical_device,
             CommandBufferPool* pool,
             GpuAllocator* allocator,
             GpuUploader* uploader);
  ~ImageCache();

  // Obtain an unused Image with the required properties.  A new Image might be
  // created, or an existing one reused.
  ImagePtr NewImage(const ImageInfo& info);

  // Return a new Image that is suitable for use as a depth attachment.  A new
  // Image might be created, or an existing one reused.
  ImagePtr NewDepthImage(vk::Format format,
                         uint32_t width,
                         uint32_t height,
                         vk::ImageUsageFlags additional_flags);

  // Return a new Image that is suitable for use as a color attachment.  A new
  // Image might be created, or an existing one reused.
  ImagePtr NewColorAttachmentImage(uint32_t width,
                                   uint32_t height,
                                   vk::ImageUsageFlags additional_flags);

  // Return new Image containing the provided pixels.  A new Image might be
  // created, or an existing one reused.  Uses transfer queue to efficiently
  // transfer image data to GPU.
  ImagePtr NewImageFromPixels(vk::Format format,
                              uint32_t width,
                              uint32_t height,
                              uint8_t* pixels);

  // Return new Image containing the provided pixels.  A new Image might be
  // created, or an existing one reused.  Uses transfer queue to efficiently
  // transfer image data to GPU.  If bytes is null, don't bother transferring.
  ImagePtr NewRgbaImage(uint32_t width, uint32_t height, uint8_t* bytes);

  // Returns RGBA image.  A new Image might be created, or an existing one
  // reused.
  ImagePtr NewCheckerboardImage(uint32_t width, uint32_t height);

  // Returns single-channel luminance image containing white noise.  A new Image
  // might be created, or an existing one reused.
  ImagePtr NewNoiseImage(uint32_t width, uint32_t height);

 private:
  // Implement ImageOwner::RecycleImage().  Adds the image to unused_images_.
  void RecycleImage(const ImageInfo& info,
                    vk::Image image,
                    impl::GpuMemPtr mem) override;

  // Try to find an unused image that meets the required specs.  If successful,
  // remove and return it.  Otherwise, return nullptr.
  ImagePtr FindImage(const ImageInfo& info);

  vk::Device device_;
  vk::PhysicalDevice physical_device_;
  vk::Queue queue_;
  CommandBufferPool* command_buffer_pool_;
  GpuAllocator* allocator_;
  GpuUploader* uploader_;

  // Keep track of the number of images that must be returned before this cache
  // can be safely destroyed.
  uint32_t outstanding_image_count_ = 0;

  // Keep track of all images that are available for reuse.
  // TODO: need some method of trimming the cache, to free images that haven't
  // been used.  Note: using a FIFO queue below (instead of a stack) will make
  // this more difficult, but there is a reason to do so: some of these images
  // may still be referenced by a pending command buffer, and reusing them in
  // FIFO order makes it less likely that a pipeline barrier will result in a
  // GPU stall.  The right approach is probably to trim the cache in the most
  // straightforward way, and profile to determine whether GPU stalls are a real
  // concern.
  struct UnusedImage {
    vk::Image image;
    GpuMemPtr mem;
  };
  std::unordered_map<ImageInfo, std::queue<UnusedImage>, Hash<ImageInfo>>
      unused_images_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ImageCache);
};

}  // namespace impl
}  // namespace escher
