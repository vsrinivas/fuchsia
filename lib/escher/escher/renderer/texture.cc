// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/texture.h"

#include "escher/impl/command_buffer.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/image.h"
#include "escher/resources/resource_life_preserver.h"

namespace escher {

const ResourceCoreTypeInfo TextureCore::kTypeInfo = {
    ResourceCoreType::kTextureCore, "TextureCore"};

TextureCore::TextureCore(ResourceLifePreserver* life_preserver,
                         const ImagePtr& image,
                         vk::Filter filter,
                         vk::ImageAspectFlags aspect_mask,
                         bool use_unnormalized_coordinates)
    : ResourceCore(life_preserver, kTypeInfo) {
  vk::Device device = vulkan_context().device;

  vk::ImageViewCreateInfo view_info;
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;
  view_info.subresourceRange.aspectMask = aspect_mask;
  view_info.format = image->format();
  view_info.image = image->get();
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

TextureCore::~TextureCore() {
  vk::Device device = vulkan_context().device;
  device.destroySampler(sampler_);
  device.destroyImageView(image_view_);
}

Texture::Texture(ResourceLifePreserver* life_preserver,
                 ImagePtr image,
                 vk::Filter filter,
                 vk::ImageAspectFlags aspect_mask,
                 bool use_unnormalized_coordinates)
    : Resource2(std::make_unique<TextureCore>(life_preserver,
                                              image,
                                              filter,
                                              aspect_mask,
                                              use_unnormalized_coordinates)),
      image_(std::move(image)),
      image_view_(core()->image_view()),
      sampler_(core()->sampler()),
      width_(image_->width()),
      height_(image_->height()) {}

void Texture::KeepDependenciesAlive(impl::CommandBuffer* command_buffer) {
  image_->KeepAlive(command_buffer);
}

Texture::~Texture() {}

}  // namespace escher
