// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_IMAGE_FACTORY_H_
#define LIB_ESCHER_VK_IMAGE_FACTORY_H_

#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/vk/gpu_allocator.h"
#include "lib/escher/vk/image.h"

namespace escher {

// ImageFactory allows clients to obtain new Images with the desired
// properties. Subclasses are free to implement custom caching/recycling
// behaviors. All images obtained from an ImageFactory must be released before
// the ImageFactory is destroyed.
class ImageFactory {
 public:
  virtual ~ImageFactory() = default;
  virtual ImagePtr NewImage(const ImageInfo& info) = 0;
};

// This default implementation allocates memory and creates a new
// Image using the provided allocator and manager. The intent is for this class
// to adapt existing GpuAllocators to the ImageFactory interface (i.e.
// equivalent to a partial bind). Classes that wish to implement their own
// caching logic should subclass ImageFactory directly, instead of injecting
// tricky subclasses of GpuAllocator and ResourceManager into this object.
class ImageFactoryAdapter final : public ImageFactory {
 public:
  ImageFactoryAdapter(GpuAllocator* allocator, ResourceManager* manager)
      : allocator_(allocator->GetWeakPtr()), manager_(manager) {}

  ImagePtr NewImage(const ImageInfo& info) final {
    FXL_DCHECK(allocator_);
    return allocator_->AllocateImage(manager_, info);
  }

 private:
  const GpuAllocatorWeakPtr allocator_;
  ResourceManager* const manager_;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_IMAGE_FACTORY_H_
