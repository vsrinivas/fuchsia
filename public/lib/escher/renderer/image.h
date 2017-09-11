// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/semaphore_wait.h"
#include "lib/escher/resources/waitable_resource.h"
#include "lib/escher/util/debug_print.h"

namespace escher {

// Full description of the size and layout of an Image.
#pragma pack(push, 1)  // As required by escher::Hash<ImageInfo>
struct ImageInfo {
  vk::Format format = vk::Format::eUndefined;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t sample_count = 1;
  vk::ImageUsageFlags usage;
  vk::MemoryPropertyFlags memory_flags =
      vk::MemoryPropertyFlagBits::eDeviceLocal;

  bool operator==(const ImageInfo& other) const {
    return format == other.format && width == other.width &&
           height == other.height && sample_count == other.sample_count &&
           usage == other.usage && memory_flags == other.memory_flags;
  }
};
#pragma pack(pop)

// An Image is a WaitableResource that encapsulates a vk::Image.
class Image : public WaitableResource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Constructor.  In some cases it is necessary to wrap an un-owned vk::Image,
  // which should not be destroyed when this Image is destroyed (e.g. when
  // working with images associated with a vk::SwapchainKHR); this is done by
  // passing nullptr as the |mem| argument.
  // If |mem| is passed and |bind_image_memory| is true, this method also binds
  // the memory to the image.
  static ImagePtr New(ResourceManager* image_owner,
                      ImageInfo info,
                      vk::Image,
                      GpuMemPtr mem,
                      bool bind_image_memory = true);

  // Returns image_ and mem_ to the owner.
  ~Image() override;

  const ImageInfo& info() const { return info_; }
  vk::Image get() const { return image_; }
  vk::Format format() const { return info_.format; }
  uint32_t width() const { return info_.width; }
  uint32_t height() const { return info_.height; }
  bool has_depth() const { return has_depth_; }
  bool has_stencil() const { return has_stencil_; }
  const GpuMemPtr& memory() const { return mem_; }
  // Offset of the Image within it's GpuMem + the offset of the GpuMem within
  // its slab.  NOTE: not the same as memory()->offset().
  vk::DeviceSize memory_offset() const;

 protected:
  // Constructor.  In some cases it is necessary to wrap an un-owned vk::Image,
  // which should not be destroyed when this Image is destroyed (e.g. when
  // working with images associated with a vk::SwapchainKHR); this is done by
  // passing nullptr as the |mem| argument.
  Image(ResourceManager* image_owner, ImageInfo info, vk::Image, GpuMemPtr mem);

 private:
  const ImageInfo info_;
  const vk::Image image_;
  GpuMemPtr mem_;
  const vk::DeviceSize mem_offset_ = 0;
  bool has_depth_;
  bool has_stencil_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Image);
};

typedef fxl::RefPtr<Image> ImagePtr;

// Debugging.
ESCHER_DEBUG_PRINTABLE(ImageInfo);

}  // namespace escher
