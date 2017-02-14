// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/texture.h"

#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/image.h"

namespace escher {

Texture::Texture(ImagePtr image, vk::Device device, vk::Filter filter)
    : Resource(nullptr),
      image_(std::move(image)),
      device_(device),
      width_(image_->width()),
      height_(image_->height()) {
  Init(filter, vk::ImageAspectFlagBits::eColor, false);
}

Texture::Texture(ImagePtr image,
                 vk::Device device,
                 vk::Filter filter,
                 vk::ImageAspectFlags aspect_mask,
                 bool use_unnormalized_coordinates)
    : Resource(nullptr),
      image_(std::move(image)),
      device_(device),
      width_(image_->width()),
      height_(image_->height()) {
  Init(filter, aspect_mask, use_unnormalized_coordinates);
}

void Texture::Init(vk::Filter filter,
                   vk::ImageAspectFlags aspect_mask,
                   bool use_unnormalized_coordinates) {
  vk::ImageViewCreateInfo view_info;
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;
  view_info.subresourceRange.aspectMask = aspect_mask;
  view_info.format = image_->format();
  view_info.image = image_->get();
  image_view_ = ESCHER_CHECKED_VK_RESULT(device_.createImageView(view_info));

  vk::SamplerCreateInfo sampler_info = {};
  sampler_info.magFilter = filter;
  sampler_info.minFilter = filter;

  sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
  sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
  sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;
  sampler_info.anisotropyEnable = false;
  sampler_info.unnormalizedCoordinates = use_unnormalized_coordinates;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = vk::CompareOp::eAlways;
  sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  sampler_info.mipLodBias = 0.0f;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = 0.0f;
  sampler_ = ESCHER_CHECKED_VK_RESULT(device_.createSampler(sampler_info));
}

Texture::~Texture() {
  device_.destroySampler(sampler_);
  device_.destroyImageView(image_view_);
}

}  // namespace escher
