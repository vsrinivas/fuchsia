// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_EVENT_POOL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_EVENT_POOL_H_

//
//
//

#include <vulkan/vulkan.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

struct vk_event_pool *
vk_event_pool_create(VkDevice                      device,
                     VkAllocationCallbacks const * allocator,
                     uint32_t const                resize);

void
vk_event_pool_release(struct vk_event_pool * const event_pool);

void
vk_event_pool_reset(struct vk_event_pool * const event_pool);

VkEvent
vk_event_pool_acquire(struct vk_event_pool * const event_pool);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_EVENT_POOL_H_
