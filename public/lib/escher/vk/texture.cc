// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/texture.h"

#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/image.h"

namespace escher {

const ResourceTypeInfo Texture::kTypeInfo("Texture",
                                          ResourceType::kResource,
                                          ResourceType::kTexture);

Texture::Texture(ResourceRecycler* resource_recycler,
                 ImagePtr image,
                 vk::Filter filter,
                 vk::ImageAspectFlags aspect_mask,
                 bool use_unnormalized_coordinates)
    : Resource(resource_recycler),
      image_(std::move(image)),
      width_(image_->width()),
      height_(image_->height()) {
  vk::Device device = vulkan_context().device;

  vk::ImageViewCreateInfo view_info;
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;
  view_info.subresourceRange.aspectMask = aspect_mask;
  view_info.format = image_->format();
  view_info.image = image_->get();
  image_view_ = ESCHER_CHECKED_VK_RESULT(device.createImageView(view_info));

  vk::SamplerCreateInfo sampler_info = {};
  sampler_info.magFilter = filter;
  sampler_info.minFilter = filter;

  sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
  sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
  sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;
  sampler_info.anisotropyEnable = false;
  sampler_info.maxAnisotropy = 1.0;
  sampler_info.unnormalizedCoordinates = use_unnormalized_coordinates;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = vk::CompareOp::eAlways;
  sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  sampler_info.mipLodBias = 0.0f;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = 0.0f;
  sampler_ = ESCHER_CHECKED_VK_RESULT(device.createSampler(sampler_info));
}

Texture::~Texture() {
  vk::Device device = vulkan_context().device;
  device.destroySampler(sampler_);
  device.destroyImageView(image_view_);
}

TexturePtr Texture::New(ResourceRecycler* resource_recycler,
                        ImagePtr image,
                        vk::Filter filter,
                        vk::ImageAspectFlags aspect_mask,
                        bool use_unnormalized_coordinates) {
  return fxl::MakeRefCounted<Texture>(resource_recycler, std::move(image),
                                      filter, aspect_mask,
                                      use_unnormalized_coordinates);
}

}  // namespace escher
