// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/renderer/semaphore_wait.h"
#include "escher/resources/resource.h"

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

// Every Image has an owner, who is responsible for cleaning up the Image's
// underlying resources when it is destroyed.  The ImageOwner must outlive all
// of its owned Images.
class ImageOwner;

class ImageCore : public ResourceCore {
 public:
  static const ResourceCoreTypeInfo kTypeInfo;

  ImageCore(ResourceCoreManager* image_owner,
            ImageInfo info,
            vk::Image,
            GpuMemPtr mem);

  ~ImageCore() override;

  const ImageInfo& info() const { return info_; }
  vk::Image image() const { return image_; }
  vk::Format format() const { return info_.format; }
  uint32_t width() const { return info_.width; }
  uint32_t height() const { return info_.height; }
  bool has_depth() const { return has_depth_; }
  bool has_stencil() const { return has_stencil_; }

 private:
  const ImageInfo info_;
  const vk::Image image_;
  GpuMemPtr mem_;
  bool has_depth_;
  bool has_stencil_;
};

// Encapsulates a vk::Image.  Lifecycle is managed by an ImageOwner.
class Image : public Resource2 {
 public:
  // Returns image_ and mem_ to the owner.
  ~Image() override;

  Image(std::unique_ptr<ImageCore> core);

  Image(ResourceCoreManager* image_owner,
        ImageInfo info,
        vk::Image,
        GpuMemPtr mem);

  // Helper function that creates a VkImage given the parameters in ImageInfo.
  // This does not bind the the VkImage to memory; the caller must do that
  // separately after calling this function.
  static vk::Image CreateVkImage(const vk::Device& device, ImageInfo info);

  vk::Image get() const { return core()->image(); }
  vk::Format format() const { return core()->format(); }
  uint32_t width() const { return core()->width(); }
  uint32_t height() const { return core()->height(); }

  bool has_depth() const { return core()->has_depth(); }
  bool has_stencil() const { return core()->has_stencil(); }

  const ImageCore* core() const {
    FTL_CHECK(Resource2::core()->type_info().IsKindOf(ImageCore::kTypeInfo));
    return static_cast<const ImageCore*>(Resource2::core());
  }

  // TODO: eventually make these private, callable only by friends.
  void SetWaitSemaphore(SemaphorePtr semaphore);
  SemaphorePtr TakeWaitSemaphore();

 private:
  void KeepDependenciesAlive(impl::CommandBuffer* command_buffer) override {}

  SemaphorePtr wait_semaphore_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Image);
};

typedef ftl::RefPtr<Image> ImagePtr;

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
