// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_sampler.h"

#include "tests/common/vk_utils.h"  // For vk() macro.

VkSampler
vk_sampler_create_linear_clamp_to_edge(VkDevice device, const VkAllocationCallbacks * allocator)
{
  VkSampler sampler;

  vk(CreateSampler(
    device,
    &(const VkSamplerCreateInfo){ .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                  .pNext                   = NULL,
                                  .flags                   = 0,
                                  .magFilter               = VK_FILTER_LINEAR,
                                  .minFilter               = VK_FILTER_LINEAR,
                                  .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                  .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                  .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                  .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                  .mipLodBias              = 0.0f,
                                  .anisotropyEnable        = VK_FALSE,
                                  .maxAnisotropy           = 0.0f,
                                  .compareEnable           = VK_FALSE,
                                  .compareOp               = VK_COMPARE_OP_ALWAYS,
                                  .minLod                  = 0.0f,
                                  .maxLod                  = 0.0f,
                                  .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
                                  .unnormalizedCoordinates = VK_TRUE },
    allocator,
    &sampler));

  return sampler;
}
