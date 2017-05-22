// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/resources/resource.h"

namespace escher {

class TextureCore : public ResourceCore {
 public:
  static const ResourceCoreTypeInfo kTypeInfo;

  TextureCore(ResourceLifePreserver* life_preserver,
              const ImagePtr& image,
              vk::Filter filter,
              vk::ImageAspectFlags aspect_mask,
              bool use_unnormalized_coordinates);
  ~TextureCore() override;

  vk::ImageView image_view() const { return image_view_; }
  vk::Sampler sampler() const { return sampler_; }

 private:
  vk::ImageView image_view_;
  vk::Sampler sampler_;
};

class Texture : public Resource2 {
 public:
  // Construct a new Texture, which encapsulates a newly-created VkImageView and
  // VkSampler.  |aspect_mask| is used to create the VkImageView, and |filter|
  // and |use_unnormalized_coordinates| are used to create the VkSampler.
  // |life_preserver| guarantees that the underlying Vulkan resources are not
  // destroyed while still referenced by a pending command buffer.
  Texture(ResourceLifePreserver* life_preserver,
          ImagePtr image,
          vk::Filter filter,
          vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor,
          bool use_unnormalized_coordinates = false);
  ~Texture() override;

  const ImagePtr& image() const { return image_; }
  vk::ImageView image_view() const { return image_view_; }
  vk::Sampler sampler() const { return sampler_; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

  const TextureCore* core() const {
    FTL_CHECK(Resource2::core()->type_info().IsKindOf(TextureCore::kTypeInfo));
    return static_cast<const TextureCore*>(Resource2::core());
  }

 private:
  void KeepDependenciesAlive(impl::CommandBuffer* command_buffer) override;

  ImagePtr image_;
  vk::Device device_;
  vk::ImageView image_view_;
  vk::Sampler sampler_;
  uint32_t width_;
  uint32_t height_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Texture);
};

typedef ftl::RefPtr<Texture> TexturePtr;

}  // namespace escher
