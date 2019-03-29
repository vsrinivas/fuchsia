// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel_result.h"

//
//
//

struct spn_device;

//
//
//

#define SPN_FENCE_COMPLETE_PFN_PAYLOAD_SIZE_MAX  (sizeof(uintptr_t) * 4)

typedef void (*spn_fence_complete_pfn)(void * payload);

//
//
//

void
spn_device_fence_pool_create(struct spn_device * const device,
                             uint32_t            const size);


void
spn_device_fence_pool_dispose(struct spn_device * const device);

//
//
//

VkFence
spn_device_fence_pool_acquire(struct spn_device    * const device,
                              VkQueue                const queue,
                              VkCommandBuffer        const cb,
                              spn_fence_complete_pfn const pfn,
                              void                 * const pfn_payload,
                              size_t                 const pfn_payload_size);

//
//
//
