// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <stdlib.h>

#include "context.h"
#include "device.h"
#include "target.h"

#include "queue_pool.h"
#include "cb_pool.h"
#include "fence_pool.h"
#include "handle_pool.h"
#include "block_pool.h"

#include "path_builder_impl.h"
#include "raster_builder_impl.h"
#include "composition_impl.h"
#include "styling_impl.h"
#include "render_impl.h"

#include "common/vk/vk_assert.h"

//
//
//

static
spn_result
spn_device_create(struct spn_context            * const context,
                  struct spn_device_vk          * const device_vk,
                  struct spn_target_image const * const target_image,
                  uint64_t                        const block_pool_size,
                  uint32_t                        const handle_count)
{
  struct spn_device * device = malloc(sizeof(*device));

  context->device = device;
  device->context = context;

  device->vk      = device_vk;
  device->target  = spn_target_create(device_vk,target_image);

  struct spn_target_config const * const config = spn_target_get_config(device->target);

  spn_allocator_host_perm_create(&device->allocator.host.perm,
                                 config->allocator.host.perm.alignment);

  spn_allocator_host_temp_create(&device->allocator.host.temp,
                                 &device->allocator.host.perm,
                                 config->allocator.host.temp.subbufs,
                                 config->allocator.host.temp.size,
                                 config->allocator.host.temp.alignment);

  spn_allocator_device_perm_create(&device->allocator.device.perm.local,
                                   device_vk,

                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,

                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  |
                                   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | // vkCmdDispatchIndirect()
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT    | // <-- notice SRC bit
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   0,
                                   NULL);

  spn_allocator_device_perm_create(&device->allocator.device.perm.copyback,
                                   device_vk,

                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_CACHED_BIT,

                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,

                                   0,
                                   NULL);

  spn_allocator_device_perm_create(&device->allocator.device.perm.coherent,
                                   device_vk,

                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // FIXME -- target configurable

                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  |
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,

                                   0,
                                   NULL);

  spn_allocator_device_temp_create(&device->allocator.device.temp.local,
                                   &device->allocator.host.perm,
                                   &device->allocator.device.perm.local,
                                   device_vk,
                                   128,
                                   32 * 1024 * 1024);


  spn_device_queue_pool_create(device,1); // FIXME - verify performance - this is a simplistic pool

  spn_device_cb_pool_create(device);      // FIXME - verify performance - this is a simplistic pool

  spn_device_fence_pool_create(device,config->fence_pool.size);

  spn_device_handle_pool_create(device,handle_count);

  spn_device_block_pool_create(device,block_pool_size,handle_count);

  return SPN_SUCCESS;
}

//
//
//

static
spn_result
spn_device_dispose(struct spn_device * const device)
{
  //
  // FIXME -- do we want to use spn_device_lost() ?
  //

  // drain all in-flight completions
  spn_device_drain(device);

  // shut down each major module in reverse order
  spn_device_block_pool_dispose(device);
  spn_device_handle_pool_dispose(device);
  spn_device_fence_pool_dispose(device);
  spn_device_cb_pool_dispose(device);
  spn_device_queue_pool_dispose(device);

  spn_allocator_device_temp_dispose(&device->allocator.device.temp.local,
                                    device->vk);

  spn_allocator_device_perm_dispose(&device->allocator.device.perm.coherent,
                                    device->vk);
  spn_allocator_device_perm_dispose(&device->allocator.device.perm.copyback,
                                    device->vk);
  spn_allocator_device_perm_dispose(&device->allocator.device.perm.local,
                                    device->vk);

  spn_allocator_host_temp_dispose(&device->allocator.host.temp);
  spn_allocator_host_perm_dispose(&device->allocator.host.perm);

  spn_target_dispose(device->target,device->vk);

  free(device->context);
  free(device);

  return SPN_SUCCESS;
}

//
//
//

uint64_t
spn_device_wait_nsecs(struct spn_device * const device)
{
  //
  // FIXME -- make this part of config
  //
  return 1000UL * 1000UL * 250UL; // 250 msecs.
}

//
// acquire a command buffer and begin
//

VkCommandBuffer
spn_device_cb_acquire_begin(struct spn_device * const device)
{
  VkCommandBuffer cb = spn_device_cb_pool_acquire(device);

  VkCommandBufferBeginInfo const cbbi = {
    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext            = NULL,
    .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    .pInheritanceInfo = NULL
  };

  vk(BeginCommandBuffer(cb,&cbbi));

  return cb;
}

//
//
//

VkFence
spn_device_cb_end_fence_acquire(struct spn_device    * const device,
                                VkCommandBuffer        const cb,
                                spn_fence_complete_pfn const pfn,
                                void                 * const pfn_payload,
                                size_t                 const pfn_payload_size)
{
  vk(EndCommandBuffer(cb));

  return spn_device_fence_pool_acquire(device,
                                       cb,
                                       pfn,
                                       pfn_payload,
                                       pfn_payload_size);
}

//
//
//

void
spn_device_lost(struct spn_device * const device)
{
  exit(-1);
}

//
//
//

spn_result
spn_context_create_vk(struct spn_context *          * const context_p,
                      struct spn_device_vk          * const device_vk,
                      struct spn_target_image const * const target_image,
                      uint64_t                        const block_pool_size,
                      uint32_t                        const handle_count)
{
  struct spn_context * context = malloc(sizeof(*context));

  *context_p                 = context;

  context->dispose           = spn_device_dispose;
  //context->reset           = spn_device_reset;
  context->yield             = spn_device_yield;
  context->wait              = spn_device_wait;

  context->path_builder      = spn_path_builder_impl_create;
  context->path_retain       = spn_device_handle_pool_validate_retain_h_paths;
  context->path_release      = spn_device_handle_pool_validate_release_h_paths;

  context->raster_builder    = spn_raster_builder_impl_create;
  context->raster_retain     = spn_device_handle_pool_validate_retain_h_rasters;
  context->raster_release    = spn_device_handle_pool_validate_release_h_rasters;

  context->composition       = spn_composition_impl_create;
  context->styling           = spn_styling_impl_create;
  context->render            = spn_render_impl;

  spn_result err = spn_device_create(context,
                                  device_vk,
                                  target_image,
                                  block_pool_size,
                                  handle_count);

  return err;
}

//
//
//
