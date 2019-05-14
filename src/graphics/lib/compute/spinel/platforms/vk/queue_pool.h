// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_QUEUE_POOL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_QUEUE_POOL_H_

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

struct spn_device;

//
//
//

void
spn_device_queue_pool_create(struct spn_device * const device, uint32_t const queue_count);

void
spn_device_queue_pool_dispose(struct spn_device * const device);

//
// FIXME -- move this to device.h
//

VkQueue
spn_device_queue_next(struct spn_device * const device);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_QUEUE_POOL_H_
