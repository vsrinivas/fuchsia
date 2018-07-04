// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/image_cache.h"

#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/gpu_uploader.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/vk/gpu_allocator.h"

namespace escher {
namespace impl {

ImageCache::ImageCache(EscherWeakPtr escher, GpuAllocator* allocator)
    : ResourceManager(escher),
      allocator_(allocator ? allocator : escher->gpu_allocator()) {}

ImageCache::~ImageCache() {}

ImagePtr ImageCache::NewImage(const ImageInfo& info) {
  if (ImagePtr result = FindImage(info)) {
    return result;
  }

  // Create a new vk::Image, since we couldn't find a suitable one.
  vk::Image image = image_utils::CreateVkImage(vk_device(), info);

  // Allocate memory and bind it to the image.
  vk::MemoryRequirements reqs = vk_device().getImageMemoryRequirements(image);
  GpuMemPtr memory = allocator_->Allocate(reqs, info.memory_flags);

  ImagePtr escher_image = Image::New(this, info, image, std::move(memory));
  FXL_CHECK(escher_image);

  return escher_image;
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
