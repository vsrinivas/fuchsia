// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_DEVICE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_DEVICE_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "allocator.h"
#include "block_pool.h"
#include "context.h"
#include "deps.h"
#include "handle_pool.h"
#include "queue_pool.h"
#include "radix_sort/platforms/vk/radix_sort_vk.h"
#include "spinel/platforms/vk/spinel_vk.h"
#include "target_instance.h"

//
//
//
struct spinel_device_vk
{
  VkPhysicalDevice              pd;
  VkDevice                      d;
  VkPipelineCache               pc;
  VkAllocationCallbacks const * ac;

  struct
  {
    struct spinel_queue_pool compute;
  } q;

  //
  // Workarounds
  //
  struct
  {
    bool mesa_21_anv;
  } workaround;
};

//
//
//
struct spinel_device
{
  //
  // Spinel abstract interface
  //
  struct spinel_context * context;

  //
  // Vulkan
  //
  struct spinel_device_vk vk;

  //
  // Device-specific Spinel target instance
  //
  struct spinel_target_instance ti;

  //
  // Allocators
  //
  struct
  {
    struct
    {
      struct
      {
        struct spinel_allocator drw;         // no host access  / device read-write
        struct spinel_allocator hw_dr;       // host write      / device read once
        struct spinel_allocator hrw_dr;      // host read-write / device read once
        struct spinel_allocator hr_dw;       // host read       / device write once
        struct spinel_allocator drw_shared;  // device read-write on 1 or 2 queue families
      } perm;
    } device;
  } allocator;

  //
  // Subsystems
  //
  struct spinel_deps *        deps;
  struct spinel_handle_pool * handle_pool;
  struct spinel_block_pool    block_pool;
};

//
// Disable device because of a fatal error
//
void
spinel_device_lost(struct spinel_device * device);

//
//
//
spinel_result_t
spinel_device_wait_all(struct spinel_device * device, bool wait_all, char const * label_name);

spinel_result_t
spinel_device_wait(struct spinel_device * device, char const * label_name);

//
//
//
void
spinel_debug_utils_cmd_begin(VkCommandBuffer cb, char const * label_name);

void
spinel_debug_utils_cmd_end(VkCommandBuffer cb);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_DEVICE_H_
