// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block_pool.h"
#include "cb_pool.h"
#include "queue_pool.h"
#include "device.h"
#include "target.h"
#include "handle.h"
#include "common/vk/vk_assert.h"

//
//
//

struct spn_block_pool
{
  struct spn_target_ds_block_pool_t      ds_block_pool;

  struct {
    VkDescriptorBufferInfo             * dbi;
    VkDeviceMemory                       dm;
  } bp_ids;
  struct {
    VkDescriptorBufferInfo             * dbi;
    VkDeviceMemory                       dm;
  } bp_blocks;
  struct {
    VkDescriptorBufferInfo             * dbi;
    VkDeviceMemory                       dm;
  } bp_host_map;

  uint32_t                               bp_size;
  uint32_t                               bp_mask;
};

//
//
//

static
uint32_t
spn_pow2_ru_u32(uint32_t n)
{
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n++;

  return n;
}

//
//
//

void
spn_device_block_pool_create(struct spn_device * const device,
                             uint64_t            const block_pool_size, // in bytes
                             uint32_t            const handle_count)
{
  struct spn_block_pool * const block_pool =
    spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  sizeof(*block_pool));

  device->block_pool = block_pool;

  struct spn_target              * const target = device->target;
  struct spn_target_config const * const config = spn_target_get_config(target);

  // block pool sizing
  uint64_t const block_pool_dwords = (block_pool_size + sizeof(uint32_t) - 1) / sizeof(uint32_t);
  uint32_t const block_dwords      = 1 << config->block_pool.block_dwords_log2;
  uint32_t const block_count       = (uint32_t)((block_pool_dwords + (block_dwords - 1)) >> config->block_pool.block_dwords_log2);
  uint32_t const id_count          = spn_pow2_ru_u32(block_count);
  uint32_t const workgroups        = (block_count + config->block_pool.ids_per_workgroup - 1) / config->block_pool.ids_per_workgroup;

  block_pool->bp_size = block_count;
  block_pool->bp_mask = id_count - 1;

  // get a descriptor set -- there is only one per Spinel device!
  spn_target_ds_acquire_block_pool(target,device,&block_pool->ds_block_pool);

  // get descriptor set DBIs
  block_pool->bp_ids.dbi      = spn_target_ds_get_block_pool_bp_ids     (target,block_pool->ds_block_pool);
  block_pool->bp_blocks.dbi   = spn_target_ds_get_block_pool_bp_blocks  (target,block_pool->ds_block_pool);
  block_pool->bp_host_map.dbi = spn_target_ds_get_block_pool_bp_host_map(target,block_pool->ds_block_pool);

  // allocate buffers
  size_t   const bp_ids_size    =
    SPN_TARGET_BUFFER_OFFSETOF(block_pool,bp_ids,bp_ids) +
    id_count * sizeof(spn_block_id_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                  device->vk,
                                  bp_ids_size,
                                  NULL,
                                  block_pool->bp_ids.dbi,
                                  &block_pool->bp_ids.dm);

  uint32_t const bp_dwords      = block_count * block_dwords;
  size_t   const bp_blocks_size =
    SPN_TARGET_BUFFER_OFFSETOF(block_pool,bp_blocks,bp_blocks) +
    bp_dwords * sizeof(uint32_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                  device->vk,
                                  bp_blocks_size,
                                  NULL,
                                  block_pool->bp_blocks.dbi,
                                  &block_pool->bp_blocks.dm);

  size_t const bp_host_map_size =
    SPN_TARGET_BUFFER_OFFSETOF(block_pool,bp_host_map,bp_host_map) +
    handle_count * sizeof(spn_handle_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                  device->vk,
                                  bp_host_map_size,
                                  NULL,
                                  block_pool->bp_host_map.dbi,
                                  &block_pool->bp_host_map.dm);

  // update the block pool
  spn_target_ds_update_block_pool(target,
                                  device->vk,
                                  block_pool->ds_block_pool);

  // get a cb
  VkCommandBuffer cb = spn_device_cb_acquire_begin(device);

  // bind the global block pool
  spn_target_ds_bind_block_pool_init_block_pool(target,cb,block_pool->ds_block_pool);

  // append push constants
  struct spn_target_push_block_pool_init const push =
    {
      .bp_size = block_pool->bp_size
    };

  spn_target_p_push_block_pool_init(target,cb,&push);

  // bind pipeline
  spn_target_p_bind_block_pool_init(target,cb);

  // dispatch the pipeline
  vkCmdDispatch(cb,workgroups,1,1);

  // end the cb and acquire a fence
  VkFence const fence = spn_device_cb_end_fence_acquire(device,cb,NULL,NULL,0UL);

  // boilerplate submit
  struct VkSubmitInfo const si = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = NULL,
    .waitSemaphoreCount   = 0,
    .pWaitSemaphores      = NULL,
    .pWaitDstStageMask    = NULL,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = 0,
    .pSignalSemaphores    = NULL
  };

  vk(QueueSubmit(spn_device_queue_next(device),1,&si,fence));

  //
  // FIXME -- continue intializing and drain later
  //
  spn_device_drain(device);
}

void
spn_device_block_pool_dispose(struct spn_device * const device)
{
  struct spn_target     * const target     = device->target;
  struct spn_block_pool * const block_pool = device->block_pool;

  spn_target_ds_release_block_pool(target,block_pool->ds_block_pool);

  spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                 device->vk,
                                 block_pool->bp_host_map.dbi,
                                 block_pool->bp_host_map.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                 device->vk,
                                 block_pool->bp_blocks.dbi,
                                 block_pool->bp_blocks.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                 device->vk,
                                 block_pool->bp_ids.dbi,
                                 block_pool->bp_ids.dm);

  spn_allocator_host_perm_free(&device->allocator.host.perm,
                               device->block_pool);
}

//
//
//

uint32_t
spn_device_block_pool_get_mask(struct spn_device * const device)
{
  return device->block_pool->bp_mask;
}

//
//
//

struct spn_target_ds_block_pool_t
spn_device_block_pool_get_ds(struct spn_device * const device)
{
  return device->block_pool->ds_block_pool;
}

//
//
//
