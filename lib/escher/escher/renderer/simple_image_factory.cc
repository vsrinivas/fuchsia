// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/simple_image_factory.h"

#include "escher/resources/resource_manager.h"
#include "escher/util/image_utils.h"
#include "escher/vk/gpu_allocator.h"

namespace escher {

SimpleImageFactory::SimpleImageFactory(ResourceManager* resource_manager,
                                       escher::GpuAllocator* allocator)
    : resource_manager_(resource_manager), allocator_(allocator) {}

SimpleImageFactory::~SimpleImageFactory() {}

ImagePtr SimpleImageFactory::NewImage(const ImageInfo& info) {
  vk::Image image =
      image_utils::CreateVkImage(resource_manager_->device(), info);

  // Allocate memory and bind it to the image.
  vk::MemoryRequirements reqs =
      resource_manager_->device().getImageMemoryRequirements(image);
  escher::GpuMemPtr memory = allocator_->Allocate(reqs, info.memory_flags);
  vk::Result result = resource_manager_->device().bindImageMemory(
      image, memory->base(), memory->offset());
  FXL_CHECK(result == vk::Result::eSuccess);

  return fxl::MakeRefCounted<Image>(resource_manager_, info, image,
                                    std::move(memory));
}

}  // namespace escher
