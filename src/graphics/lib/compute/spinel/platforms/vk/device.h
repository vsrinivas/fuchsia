// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_DEVICE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_DEVICE_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "allocator_device.h"
#include "allocator_host.h"
#include "fence_pool.h"
#include "spinel_types.h"
#include "spinel_vk.h"

//
//
//

struct spn_device
{
  struct spn_vk_environment * environment;  // Vulkan environment
  struct spn_context *        context;      // Spinel abstract interface
  struct spn_vk *             instance;     // Instance of target state and resources

  struct
  {
    struct
    {
      struct spn_allocator_host_perm perm;
      struct spn_allocator_host_temp temp;
    } host;
    struct
    {
      struct
      {
        struct spn_allocator_device_perm local;
        struct spn_allocator_device_perm copyback;  // hrN     -- copy-back to host
        struct spn_allocator_device_perm coherent;  // hw1:drN -- target-specific
      } perm;
      struct
      {
        struct spn_allocator_device_temp local;
      } temp;
    } device;
  } allocator;

  struct spn_queue_pool *  queue_pool;
  struct spn_cb_pool *     cb_pool;
  struct spn_fence_pool *  fence_pool;
  struct spn_handle_pool * handle_pool;
  struct spn_block_pool *  block_pool;

  //
  //
  //
#if 0
  struct spn_scheduler * scheduler;
  struct spn_grid_deps * deps;
  struct hs_cl const *   hs;  // opaque hotsort
#endif
};

//
// FIXME -- Spinel target needs to be able to vend what extensions it
// requires from a target device
//

//
// Creation and disposal intitializes the context and may rely on
// other context resources like the scheduler
//

//
// Disable device because of a fatal error
//

void
spn_device_lost(struct spn_device * const device);

//
//
//

uint64_t
spn_device_wait_nsecs(struct spn_device * const device);

//
// does this need to be here?  just grab config
//

uint32_t
spn_device_block_pool_get_mask(struct spn_device * const device);

//
// Acquire and begin a command buffer
//

VkCommandBuffer
spn_device_cb_acquire_begin(struct spn_device * const device);

//
// End a command buffer and acquire a fence
//

VkFence
spn_device_cb_end_fence_acquire(struct spn_device * const    device,
                                VkCommandBuffer const        cb,
                                spn_fence_complete_pfn const pfn,
                                void * const                 pfn_payload,
                                size_t const                 pfn_payload_size);

//
// yield : if there are unsignaled fences, test if at least one fence is signaled
// wait  : if there are unsignaled fences, wait for at least one fence to signal
// drain : wait for all unsignaled fences -- unknown if we need this
//

spn_result
spn_device_yield(struct spn_device * const device);

spn_result
spn_device_wait(struct spn_device * const device);

spn_result
spn_device_drain(struct spn_device * const device);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_DEVICE_H_
