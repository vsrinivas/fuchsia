// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/image.h"

#include "escher/impl/gpu_mem.h"
#include "escher/renderer/image_owner.h"

namespace escher {

ImageInfo::ImageInfo(const vk::ImageCreateInfo& create_info)
    : format(create_info.format),
      width(create_info.extent.width),
      height(create_info.extent.height) {}

ImageInfo::ImageInfo(vk::Format f, uint32_t w, uint32_t h)
    : format(f), width(w), height(h) {}

Image::Image(ImageInfo info,
             vk::Image image,
             impl::GpuMemPtr mem,
             ImageOwner* owner)
    : impl::Resource(nullptr),
      info_(info),
      image_(image),
      mem_(std::move(mem)),
      owner_(owner),
      has_depth_(false),
      has_stencil_(false) {
  FTL_CHECK(image);
  FTL_CHECK(owner);

  // TODO: How do we future-proof this in case more formats are added?
  switch (info_.format) {
    case vk::Format::eD16Unorm:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat:
      has_depth_ = true;
    case vk::Format::eS8Uint:
      has_stencil_ = true;
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
      has_depth_ = true;
      has_stencil_ = true;
    default:
      // No depth or stencil component.
      break;
  }
}

Image::~Image() {
  // Return underlying resources to the manager.
  owner_->RecycleImage(info_, image_, std::move(mem_));
}

}  // namespace escher
