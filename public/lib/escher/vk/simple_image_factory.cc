// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/simple_image_factory.h"

#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/vk/gpu_allocator.h"

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
  ImagePtr escher_image =
      Image::New(resource_manager_, info, image, std::move(memory));
  FXL_CHECK(escher_image);
  return escher_image;
}

}  // namespace escher
