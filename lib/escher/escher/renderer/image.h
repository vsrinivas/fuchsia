// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/renderer/semaphore_wait.h"
#include "escher/resources/resource.h"
#include "escher/util/debug_print.h"

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

// An Image is a Resource that encapsulates a vk::Image.
class Image : public Resource2 {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Returns image_ and mem_ to the owner.
  ~Image() override;

  // Constructor.  In some cases it is necessary to wrap an un-owned vk::Image,
  // which should not be destroyed when this Image is destroyed (e.g. when
  // working with images associated with a vk::SwapchainKHR); this is done by
  // passing nullptr as the |mem| argument.
  Image(ResourceManager* image_owner, ImageInfo info, vk::Image, GpuMemPtr mem);

  // Helper function that creates a VkImage given the parameters in ImageInfo.
  // This does not bind the the VkImage to memory; the caller must do that
  // separately after calling this function.
  static vk::Image CreateVkImage(const vk::Device& device, ImageInfo info);

  const ImageInfo& info() const { return info_; }
  vk::Image get() const { return image_; }
  vk::Format format() const { return info_.format; }
  uint32_t width() const { return info_.width; }
  uint32_t height() const { return info_.height; }
  bool has_depth() const { return has_depth_; }
  bool has_stencil() const { return has_stencil_; }

  // TODO: eventually make these private, callable only by friends.
  void SetWaitSemaphore(SemaphorePtr semaphore);
  SemaphorePtr TakeWaitSemaphore();

 private:
  const ImageInfo info_;
  const vk::Image image_;
  GpuMemPtr mem_;
  bool has_depth_;
  bool has_stencil_;

  SemaphorePtr wait_semaphore_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Image);
};

typedef ftl::RefPtr<Image> ImagePtr;

// Debugging.
ESCHER_DEBUG_PRINTABLE(ImageInfo);

// Inline function definitions.

inline void Image::SetWaitSemaphore(SemaphorePtr semaphore) {
  // This is not necessarily an error, but the consequences will depend on the
  // specific usage-pattern that first triggers it; we'll deal with it then.
  FTL_CHECK(!wait_semaphore_);
  wait_semaphore_ = std::move(semaphore);
}

inline SemaphorePtr Image::TakeWaitSemaphore() {
  return std::move(wait_semaphore_);
}

}  // namespace escher
