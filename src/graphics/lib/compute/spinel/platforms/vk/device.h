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
#include "spinel/spinel_vk.h"

//
//
//

struct spn_device
{
  struct spn_vk_environment environment;  // Vulkan environment
  struct spn_context *      context;      // Spinel abstract interface
  struct spn_vk *           instance;     // Instance of target state and resources
  struct hotsort_vk *       hs;           // HotSort instance

  struct
  {
    struct
    {
      struct spn_allocator_host_perm perm;
    } host;

    struct
    {
      struct
      {
        struct spn_allocator_device_perm drw;     // device read-write
        struct spn_allocator_device_perm hw_dr;   // host write / device read once
        struct spn_allocator_device_perm hrw_dr;  // host read-write / device read once
        struct spn_allocator_device_perm hr_dw;   // host read / device write once
      } perm;

      struct
      {
        struct spn_allocator_device_temp drw;  // device read-write
      } temp;
    } device;

  } allocator;

  struct spn_queue_pool *        queue_pool;
  struct spn_handle_pool *       handle_pool;
  struct spn_dispatch *          dispatch;
  struct spn_block_pool *        block_pool;
  struct spn_status_block_pool * status;
};

//
// Disable device because of a fatal error
//

void
spn_device_lost(struct spn_device * const device);

//
//
//

uint64_t
spn_device_get_timeout_ns(struct spn_device * const device);

//
//
//

spn_result_t
spn_device_wait_all(struct spn_device * const device,
                    bool const                wait_all,
                    char const * const        label_name);

spn_result_t
spn_device_wait(struct spn_device * const device, char const * const label_name);

//
//
//

void
spn_debug_utils_cmd_begin(VkCommandBuffer cb, char const * const label_name);

void
spn_debug_utils_cmd_end(VkCommandBuffer cb);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_DEVICE_H_
