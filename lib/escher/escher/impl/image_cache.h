// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "ftl/memory/ref_counted.h"

namespace escher {
namespace impl {

class ImageCache;

// Encapsulate an image and the memory that it is bound to.
// TODO: make public?
class Image : public ftl::RefCountedThreadSafe<Image> {
 public:
  ~Image();
  vk::Image image() { return image_; }

 private:
  friend class ImageCache;
  Image(ImageCache* cache, vk::Image image, vk::DeviceMemory memory);

  ImageCache* cache_;
  vk::Image image_;
  vk::DeviceMemory memory_;

  // TODO: make moveable?  Or make ref-countable?
  FTL_DISALLOW_COPY_AND_ASSIGN(Image);
};

// Allow client to obtain new or recycled Images.  All Images obtained from an
// ImageCache must be destroyed before the ImageCache is destroyed.
// TODO: cache returned images so that we don't need to reallocate new ones.
class ImageCache {
 public:
  ImageCache(vk::Device device, vk::PhysicalDevice physical_device);
  ~ImageCache();
  ftl::RefPtr<Image> GetImage(const vk::ImageCreateInfo& info);

  ftl::RefPtr<Image> GetDepthImage(vk::Format format,
                                   uint32_t width,
                                   uint32_t height);

 private:
  friend class Image;
  void DestroyImage(vk::Image image, vk::DeviceMemory memory);

  vk::Device device_;
  vk::PhysicalDevice physical_device_;
  uint32_t image_count_ = 0;
};

}  // namespace impl
}  // namespace escher
