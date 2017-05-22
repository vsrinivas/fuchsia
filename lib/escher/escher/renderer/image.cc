// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/image.h"

#include "escher/impl/vulkan_utils.h"
#include "escher/vk/gpu_mem.h"

namespace escher {

const ResourceCoreTypeInfo ImageCore::kTypeInfo = {ResourceCoreType::kImageCore,
                                                   "ImageCore"};

ImageCore::ImageCore(ResourceCoreManager* image_owner,
                     ImageInfo info,
                     vk::Image image,
                     GpuMemPtr mem)
    : ResourceCore(image_owner, kTypeInfo),
      info_(info),
      image_(image),
      mem_(std::move(mem)) {
  // TODO: How do we future-proof this in case more formats are added?
  switch (info.format) {
    case vk::Format::eD16Unorm:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat:
      has_depth_ = true;
      has_stencil_ = false;
      break;
    case vk::Format::eS8Uint:
      has_depth_ = false;
      has_stencil_ = true;
      break;
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
      has_depth_ = true;
      has_stencil_ = true;
      break;
    default:
      FTL_DCHECK(false);
      // No depth or stencil component.
      has_depth_ = false;
      has_stencil_ = false;
      break;
  }
}

ImageCore::~ImageCore() {
  if (!mem_) {
    // Probably a swapchain image.  We don't own the image or the memory.
    FTL_LOG(INFO) << "Destroying ImageCore with unowned VkImage (perhaps a "
                     "swapchain image?)";
  } else {
    vulkan_context().device.destroyImage(image_);
  }
}

Image::Image(std::unique_ptr<ImageCore> core) : Resource2(std::move(core)) {}

Image::Image(ResourceCoreManager* image_owner,
             ImageInfo info,
             vk::Image vk_image,
             GpuMemPtr mem)
    : Image(std::make_unique<escher::ImageCore>(image_owner,
                                                info,
                                                vk_image,
                                                mem)) {}

Image::~Image() {}

vk::Image Image::CreateVkImage(const vk::Device& device, ImageInfo info) {
  vk::ImageCreateInfo create_info;
  create_info.imageType = vk::ImageType::e2D;
  create_info.format = info.format;
  create_info.extent = vk::Extent3D{info.width, info.height, 1};
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  switch (info.sample_count) {
    case 1:
      create_info.samples = vk::SampleCountFlagBits::e1;
      break;
    case 2:
      create_info.samples = vk::SampleCountFlagBits::e2;
      break;
    case 4:
      create_info.samples = vk::SampleCountFlagBits::e4;
      break;
    case 8:
      create_info.samples = vk::SampleCountFlagBits::e8;
      break;
    default:
      FTL_DCHECK(false);
  }
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = info.usage;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;
  vk::Image image = ESCHER_CHECKED_VK_RESULT(device.createImage(create_info));
  return image;
}

}  // namespace escher
