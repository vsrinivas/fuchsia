// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SAMPLER_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SAMPLER_H_

#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// Create a new VkSampler with default settings of linear filtering with
// clamping to the edges.
extern VkSampler
vk_sampler_create_linear_clamp_to_edge(VkDevice device, const VkAllocationCallbacks * allocator);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SAMPLER_H_
