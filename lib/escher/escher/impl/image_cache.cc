// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/image_cache.h"

#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/gpu_uploader.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/util/image_utils.h"
#include "escher/vk/gpu_allocator.h"

namespace escher {
namespace impl {

ImageCache::ImageCache(Escher* escher, GpuAllocator* allocator)
    : ResourceManager(escher),
      allocator_(allocator ? allocator : escher->gpu_allocator()) {}

ImageCache::~ImageCache() {}

ImagePtr ImageCache::NewImage(const ImageInfo& info) {
  if (ImagePtr result = FindImage(info)) {
    return result;
  }

  // Create a new vk::Image, since we couldn't find a suitable one.
  vk::Image image = image_utils::CreateVkImage(device(), info);

  // Allocate memory and bind it to the image.
  vk::MemoryRequirements reqs = device().getImageMemoryRequirements(image);
  GpuMemPtr memory = allocator_->Allocate(reqs, info.memory_flags);
  vk::Result result =
      device().bindImageMemory(image, memory->base(), memory->offset());
  FXL_CHECK(result == vk::Result::eSuccess);

  return fxl::MakeRefCounted<Image>(this, info, image, std::move(memory));
}

ImagePtr ImageCache::FindImage(const ImageInfo& info) {
  auto& queue = unused_images_[info];
  if (queue.empty()) {
    return ImagePtr();
  } else {
    ImagePtr result(queue.front().release());
    queue.pop();
    return result;
  }
}

void ImageCache::OnReceiveOwnable(std::unique_ptr<Resource> resource) {
  FXL_DCHECK(resource->IsKindOf<Image>());
  std::unique_ptr<Image> image(static_cast<Image*>(resource.release()));
  auto& queue = unused_images_[image->info()];
  queue.push(std::move(image));
}

}  // namespace impl
}  // namespace escher
