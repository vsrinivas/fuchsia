// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_BARRIER_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_BARRIER_H_

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

void
vk_barrier_compute_w_to_compute_r(VkCommandBuffer cb);

void
vk_barrier_compute_w_to_transfer_r(VkCommandBuffer cb);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_VK_BARRIER_H_
