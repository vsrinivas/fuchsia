// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/forward_declarations.h"
#include "escher/impl/gpu_mem.h"
#include "escher/renderer/image.h"
#include "ftl/macros.h"
#include "ftl/memory/ref_counted.h"

namespace escher {
namespace impl {

class CommandBufferPool;

// Allow client to obtain new or recycled Images.  All Images obtained from an
// ImageCache must be destroyed before the ImageCache is destroyed.
// TODO: cache returned images so that we don't need to reallocate new ones.
class ImageCache {
 public:
  // The allocator is used to allocate memory for newly-created images.  The
  // queue and CommandBufferPool are used to schedule image layout transitions.
  ImageCache(vk::Device device,
             vk::PhysicalDevice physical_device,
             vk::Queue queue,
             GpuAllocator* allocator,
             CommandBufferPool* command_buffer_pool);
  ~ImageCache();
  ImagePtr NewImage(const vk::ImageCreateInfo& info);

  ImagePtr GetDepthImage(vk::Format format, uint32_t width, uint32_t height);

  // Performs a layout transition.  See section 11.4 of the Vulkan spec.
  void TransitionImageLayout(const ImagePtr& image,
                             vk::ImageLayout old_layout,
                             vk::ImageLayout new_layout);

 private:
  class Image : public escher::Image {
   public:
    Image(vk::Image image,
          vk::Format format,
          uint32_t width,
          uint32_t height,
          GpuMemPtr memory,
          ImageCache* cache);
    ~Image();

   private:
    ImageCache* cache_;
    GpuMemPtr memory_;

    FTL_DISALLOW_COPY_AND_ASSIGN(Image);
  };

  void DestroyImage(vk::Image image, vk::Format format);

  vk::Device device_;
  vk::PhysicalDevice physical_device_;
  vk::Queue queue_;
  GpuAllocator* allocator_;
  CommandBufferPool* command_buffer_pool_;
  uint32_t image_count_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(ImageCache);
};

}  // namespace impl
}  // namespace escher
