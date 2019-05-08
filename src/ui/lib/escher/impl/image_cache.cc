// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/image_cache.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"

namespace escher {
namespace impl {

ImageCache::ImageCache(EscherWeakPtr escher, GpuAllocator* allocator)
    : ResourceManager(escher), allocator_(allocator) {
  FXL_DCHECK(allocator_);
}

ImageCache::~ImageCache() {}

ImagePtr ImageCache::NewImage(const ImageInfo& info, GpuMemPtr* out_ptr) {
  if (out_ptr) {
    FXL_DCHECK(false) << "ImageCache does not support dedicated allocations, "
                         "creating a non-cached image";
    return allocator_->AllocateImage(escher()->resource_recycler(), info,
                                     out_ptr);
  }

  if (ImagePtr result = FindImage(info)) {
    return result;
  }

  return allocator_->AllocateImage(this, info);
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
