// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "device.h"

#include <memory.h>
#include <stdlib.h>

#include "common/vk/assert.h"
#include "context.h"
#include "spinel/spinel_assert.h"

//
//
//
#include "composition_impl.h"
#include "handle_pool.h"
#include "path_builder_impl.h"
#include "raster_builder_impl.h"
#include "styling_impl.h"
#include "swapchain_impl.h"

//
//
//
void
spinel_device_lost(struct spinel_device * const device)
{
  //
  // FIXME(allanmac): Properly shutting down Spinel is WIP.
  //
  abort();
}

//
// FIXME(allanmac): This workaround exacts some performance. Remove it as soon
// as it's feasible.
//
static void
spinel_deps_workaround_mesa_21_anv(struct spinel_device * const device)
{
  VkPhysicalDeviceVulkan12Properties pdp12 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
  };

  VkPhysicalDeviceProperties2 pdp2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &pdp12,
  };

  vkGetPhysicalDeviceProperties2(device->vk.pd, &pdp2);

  if ((pdp2.properties.vendorID == 0x8086) && strcmp(pdp12.driverName, "Mesa 21."))
    {
      device->vk.workaround.mesa_21_anv = true;
    }
}

//
//
//
static struct spinel_device *
spinel_device_create(struct spinel_vk_context_create_info const * create_info)
{
  struct spinel_device * device = malloc(sizeof(*device));

  //
  // Create the Spinel target instance
  //
  if (!spinel_target_instance_create(&device->ti,
                                     create_info->vk.d,
                                     create_info->vk.ac,
                                     create_info->vk.pc,
                                     create_info->target))
    {
      free(device);

      return NULL;
    }

  //
  // Save the Vulkan handles
  //
  device->vk.pd = create_info->vk.pd;
  device->vk.d  = create_info->vk.d;
  device->vk.pc = create_info->vk.pc;
  device->vk.ac = create_info->vk.ac;

  //////////////////////////////////////////////////////////////////////////////
  //
  // Initialize all workarounds
  //
  spinel_deps_workaround_mesa_21_anv(device);

  //
  // Create the queue pools
  //
  assert(create_info->vk.q.compute.count > 0);  // Compute queue count must be greater than zero

  spinel_queue_pool_create(&device->vk.q.compute, create_info->vk.d, &create_info->vk.q.compute);

  //
  // The allocators depend on the target config
  //
  struct spinel_target_config const * const config = &device->ti.config;

  //
  // Device allocators
  //
  // "perm device read-write"
  //
  spinel_allocator_create(&device->allocator.device.perm.drw,
                          config->allocator.device.drw.properties,
                          config->allocator.device.drw.usage,
                          VK_SHARING_MODE_EXCLUSIVE,
                          0,
                          NULL);

  //
  // "perm host write / device read"
  //
  spinel_allocator_create(&device->allocator.device.perm.hw_dr,
                          config->allocator.device.hw_dr.properties,
                          config->allocator.device.hw_dr.usage,
                          VK_SHARING_MODE_EXCLUSIVE,
                          0,
                          NULL);

  //
  // "perm host read-write / device read"
  //
  spinel_allocator_create(&device->allocator.device.perm.hrw_dr,
                          config->allocator.device.hrw_dr.properties,
                          config->allocator.device.hrw_dr.usage,
                          VK_SHARING_MODE_EXCLUSIVE,
                          0,
                          NULL);

  //
  // "perm device read-write on 1 or 2 queue families"
  //
  spinel_allocator_create(&device->allocator.device.perm.drw_shared,
                          config->allocator.device.drw_shared.properties,
                          config->allocator.device.drw_shared.usage,
                          config->swapchain.sharing_mode,
                          create_info->vk.q.shared.queue_family_count,
                          create_info->vk.q.shared.queue_family_indices);

  //
  // Create deps
  //
  struct spinel_deps_create_info const dci = {
    .semaphores = {
      .immediate = {
        .pool = {
          .size   = config->deps.semaphores.immediate.pool.size,
          .count  = config->deps.semaphores.immediate.pool.count
        },
      },
      .delayed = {
        .size     = config->deps.semaphores.delayed.size
      }
    },
    .handle_count = create_info->handle_count
  };

  device->deps = spinel_deps_create(&dci, &device->vk);

  //
  // Create the handle pool
  //
  spinel_device_handle_pool_create(device, create_info->handle_count);

  //
  // Create the block pool
  //
  // The block pool depends on the allocated handle count and not the
  // create_info->handle_count.
  //
  spinel_device_block_pool_create(device,
                                  create_info->block_pool_size,
                                  spinel_handle_pool_get_handle_count(device->handle_pool));

  //
  // Drain all submitted deps...
  //
  spinel_deps_drain_all(device->deps, &device->vk);

  return device;
}

//
//
//

static spinel_result_t
spinel_device_dispose(struct spinel_device * const device)
{
  //
  // TODO(allanmac): Alternatively, just use spinel_device_lost() to clear the
  // device and make creation/disposal a two-step process with a Spinel instance
  // and a Spinel device.
  //

  //
  // There should be zero in-flight dispatches because every Spinel user-object
  // (path builder, raster builder, styling, compute, etc.) should be draining
  // its own submissions before destruction.
  //
  // The handle pool implicitly drains its in-flight dispatchse.
  //
  spinel_device_handle_pool_dispose(device);

  //
  // make sure there are no undrained dispatches
  //
  assert(!spinel_deps_drain_1(device->deps, &device->vk));

  //
  // shut down each major module in reverse order
  //
  spinel_device_block_pool_dispose(device);
  spinel_deps_dispose(device->deps, &device->vk);
  spinel_queue_pool_dispose(&device->vk.q.compute);

  //
  // dispose spinel target instance
  //
  spinel_target_instance_destroy(&device->ti, device->vk.d, device->vk.ac);

  //
  // free context
  //
  free(device->context);

  //
  // free device
  //
  free(device);

  return SPN_SUCCESS;
}

//
//
//
static spinel_result_t
spinel_device_get_limits(struct spinel_device * device, spinel_context_limits_t * limits)
{
  struct spinel_target_config const * const config = &device->ti.config;

  //
  //
  //
  *limits = (spinel_context_limits_t){

    .global_transform = { .sx  = 0.0f,
                          .shx = (float)(1 << config->pixel.width_log2),
                          .tx  = 0.0f,
                          .shy = (float)(1 << config->pixel.height_log2),
                          .sy  = 0.0f,
                          .ty  = 0.0f,
                          .w0  = 0.0f,
                          .w1  = 0.0f },
    .tile = {
      .width  = 1u << config->tile.width_log2,
      .height = 1u << config->tile.height_log2,
    },
    .extent = {
      .width  = 1u << (config->tile.width_log2 + SPN_TTCK_HI_BITS_X),
      .height = 1u << (config->tile.height_log2 + SPN_TTCK_HI_BITS_Y),
    },
  };

  return SPN_SUCCESS;
}

//
//
//

spinel_context_t
spinel_vk_context_create(struct spinel_vk_context_create_info const * create_info)
{
  //
  // Create device
  //
  struct spinel_device * device = spinel_device_create(create_info);

  if (device == NULL)
    {
      return NULL;
    }

  spinel_context_t context = malloc(sizeof(*context));

  //
  // Init platform pfns
  //
  *context = (struct spinel_context){

    .dispose        = spinel_device_dispose,
    .get_limits     = spinel_device_get_limits,
    .path_builder   = spinel_path_builder_impl_create,
    .path_retain    = spinel_device_validate_retain_h_paths,
    .path_release   = spinel_device_validate_release_h_paths,
    .raster_builder = spinel_raster_builder_impl_create,
    .raster_retain  = spinel_device_validate_retain_h_rasters,
    .raster_release = spinel_device_validate_release_h_rasters,
    .composition    = spinel_composition_impl_create,
    .styling        = spinel_styling_impl_create,
    .swapchain      = spinel_swapchain_impl_create,
    .refcount       = 1
  };

  //
  // Connect context<>device
  //
  context->device = device;
  device->context = context;

  return context;
}

//
//
//
