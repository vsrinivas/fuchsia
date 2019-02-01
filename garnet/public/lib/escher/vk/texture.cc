// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/texture.h"

#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/image.h"

namespace escher {

const ResourceTypeInfo Texture::kTypeInfo("Texture", ResourceType::kResource,
                                          ResourceType::kImageView,
                                          ResourceType::kTexture);

Texture::Texture(ResourceRecycler* resource_recycler, ImagePtr image,
                 vk::Filter filter, vk::ImageAspectFlags aspect_mask,
                 bool use_unnormalized_coordinates)
    : ImageView(resource_recycler, std::move(image), aspect_mask) {
  vk::SamplerCreateInfo sampler_info = {};
  sampler_info.magFilter = filter;
  sampler_info.minFilter = filter;

  sampler_info.anisotropyEnable = false;
  sampler_info.maxAnisotropy = 1.0;
  sampler_info.unnormalizedCoordinates = use_unnormalized_coordinates;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = vk::CompareOp::eAlways;
  sampler_info.mipLodBias = 0.0f;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = 0.0f;
  if (use_unnormalized_coordinates) {
    sampler_info.mipmapMode = vk::SamplerMipmapMode::eNearest;
    sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  } else {
    sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
    sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
    sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
    sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;
  }
  sampler_ = ESCHER_CHECKED_VK_RESULT(vk_device().createSampler(sampler_info));
}

Texture::~Texture() { vk_device().destroySampler(sampler_); }

TexturePtr Texture::New(ResourceRecycler* resource_recycler, ImagePtr image,
                        vk::Filter filter, vk::ImageAspectFlags aspect_mask,
                        bool use_unnormalized_coordinates) {
  return fxl::MakeRefCounted<Texture>(resource_recycler, std::move(image),
                                      filter, aspect_mask,
                                      use_unnormalized_coordinates);
}

}  // namespace escher
