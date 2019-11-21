// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block_pool.h"

#include "common/vk/assert.h"
#include "device.h"
#include "dispatch.h"
#include "handle_pool.h"
#include "queue_pool.h"
#include "spinel_assert.h"
#include "vk.h"
#include "vk_target.h"

//
//
//

#ifdef SPN_BP_DEBUG

#include <stdio.h>

#include "common/vk/barrier.h"

#define SPN_BP_DEBUG_SIZE ((size_t)1 << 24)

#endif

//
//
//

struct spn_block_pool
{
  struct spn_vk_ds_block_pool_t ds_block_pool;

#ifdef SPN_BP_DEBUG
  struct
  {
    struct
    {
      VkDescriptorBufferInfo * dbi;
      VkDeviceMemory           dm;
    } d;
    struct
    {
      VkDescriptorBufferInfo dbi;
      VkDeviceMemory         dm;

      SPN_VK_BUFFER_NAME(block_pool, bp_debug) * mapped;
    } h;
  } bp_debug;
#endif

  struct
  {
    VkDescriptorBufferInfo * dbi;
    VkDeviceMemory           dm;
  } bp_ids;

  struct
  {
    VkDescriptorBufferInfo * dbi;
    VkDeviceMemory           dm;
  } bp_blocks;

  struct
  {
    VkDescriptorBufferInfo * dbi;
    VkDeviceMemory           dm;
  } bp_host_map;

  uint32_t bp_size;
  uint32_t bp_mask;
};

//
//
//

static uint32_t
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

#ifdef SPN_BP_DEBUG

void
spn_device_block_pool_debug_snap(struct spn_device * const device, VkCommandBuffer cb)
{
  VkBufferCopy const bc = {

    .srcOffset = 0,
    .dstOffset = 0,
    .size      = SPN_VK_BUFFER_OFFSETOF(block_pool, bp_debug, bp_debug) + SPN_BP_DEBUG_SIZE
  };

  vk_barrier_debug(cb);

  vkCmdCopyBuffer(cb,
                  device->block_pool->bp_debug.d.dbi->buffer,
                  device->block_pool->bp_debug.h.dbi.buffer,
                  1,
                  &bc);

  vk_barrier_debug(cb);

  // vk_barrier_transfer_w_to_host_r(cb);
}

void
spn_device_block_pool_debug_print(struct spn_device * const device)
{
  struct spn_vk_target_config const * const     config = spn_vk_get_config(device->instance);
  struct spn_vk_buf_block_pool_bp_debug const * mapped = device->block_pool->bp_debug.h.mapped;
  uint32_t const                                count  = mapped->bp_debug_count[0];

  //
  // HEX
  //
#if 1
  {
    uint32_t const subgroup_size =
      MIN_MACRO(uint32_t, 32, 1 << config->p.group_sizes.named.paths_copy.subgroup_log2);

    fprintf(stderr, "[ %u ] = {", count);

    for (uint32_t ii = 0; ii < count; ii++)
      {
        if ((ii % subgroup_size) == 0)
          fprintf(stderr, "\n");

        fprintf(stderr, "%08X, ", mapped->bp_debug[ii]);
      }

    fprintf(stderr, "\n}\n");
  }
#endif

  //
  // INT
  //
#if 0
  {
    uint32_t const subgroup_size =
      MIN_MACRO(uint32_t, 32, 1 << config->p.group_sizes.named.paths_copy.subgroup_log2);

    fprintf(stderr, "[ %u ] = {", count);

    for (uint32_t ii = 0; ii < count; ii++)
      {
        if ((ii % subgroup_size) == 0)
          fprintf(stderr, "\n");

        fprintf(stderr, "%11d, ", mapped->bp_debug[ii]);
      }

    fprintf(stderr, "\n}\n");
  }
#endif

  //
  // FLOAT
  //
#if 0
  {
    uint32_t const subgroup_size =
      MIN_MACRO(uint32_t, 32, 1 << config->p.group_sizes.named.paths_copy.subgroup_log2);

    fprintf(stderr, "[ %u ] = {", count);

    float const * bp_debug_float = (float *)mapped->bp_debug;

    for (uint32_t ii = 0; ii < count; ii++)
      {
        if ((ii % subgroup_size) == 0)
          fprintf(stderr, "\n");

        fprintf(stderr, "%10.2f, ", bp_debug_float[ii]);
      }

    fprintf(stderr, "\n}\n");
  }
#endif

  //
  // COORDS
  //
#if 0
  {
    // FILE * file = fopen("debug.segs", "w");

    float const * bp_debug_float = (float *)mapped->bp_debug;

    for (uint32_t ii = 0; ii < count; ii += 4)
      {
        fprintf(stderr,
                "{ { %10.2f, %10.2f }, { %10.2f, %10.2f } }\n",
                bp_debug_float[ii + 0],
                bp_debug_float[ii + 1],
                bp_debug_float[ii + 2],
                bp_debug_float[ii + 3]);
      }

    // fclose(file);
  }
#endif

  //
  // TTS
  //
#if 0
  fprintf(stderr,"[ %u ] = {", count);

  for (uint32_t ii = 2; ii < count; ii += 2)
    {
      if ((ii % 2) == 0)
        fprintf(stderr,"\n");

      union spn_tts const tts = { .u32 = mapped->bp_debug[ii + 1] };

      fprintf(stderr,"%07X : %08X : < %4u | %3d | %4u | %3d > ",
             mapped->bp_debug[ii + 0],
             tts.u32,
             tts.tx,
             tts.dx,
             tts.ty,
             tts.dy);
    }

  fprintf(stderr,"\n}\n");
#endif

  //
  // TTRK
  //
#if 0
  fprintf(stderr,"[ %u ] = {", count);

  for (uint32_t ii = 0; ii < count; ii += 2)
    {
      if ((ii % 2) == 0)
        fprintf(stderr,"\n");

      union spn_ttrk const ttrk = { .u32v2 = { .x = mapped->bp_debug[ii + 0],
                                               .y = mapped->bp_debug[ii + 1] } };

      fprintf(stderr,"%08X%08X : < %08X : %4u : %4u : %4u >\n",
             ttrk.u32v2.y,
             ttrk.u32v2.x,
             ttrk.ttsb_id,
             (uint32_t)ttrk.y,
             ttrk.x,
             ttrk.cohort);
    }

  fprintf(stderr,"\n}\n");
#endif
}

#endif

//
//
//

void
spn_device_block_pool_create(struct spn_device * const device,
                             uint64_t const            block_pool_size,  // in bytes
                             uint32_t const            handle_count)
{
  struct spn_block_pool * const block_pool =
    spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  sizeof(*block_pool));

  device->block_pool = block_pool;

  struct spn_vk * const                     instance = device->instance;
  struct spn_vk_target_config const * const config   = spn_vk_get_config(instance);

  // how large is this target's block?
  uint32_t const block_dwords_log2 = config->block_pool.block_dwords_log2;
  uint32_t const block_dwords      = 1 << block_dwords_log2;

  // block pool sizing
  uint64_t const block_pool_size_pad = block_pool_size + sizeof(uint32_t) - 1;
  uint32_t const block_pool_dwords   = (uint32_t)(block_pool_size_pad / sizeof(uint32_t));
  uint32_t const block_count         = (block_pool_dwords + block_dwords - 1) >> block_dwords_log2;
  uint32_t const id_count            = spn_pow2_ru_u32(block_count);

  block_pool->bp_size = block_count;
  block_pool->bp_mask = id_count - 1;  // ids ring is power-of-two

  // get a descriptor set -- there is only one per Spinel device!
  spn_vk_ds_acquire_block_pool(instance, device, &block_pool->ds_block_pool);

  // get descriptor set DBIs
  block_pool->bp_ids.dbi = spn_vk_ds_get_block_pool_bp_ids(instance, block_pool->ds_block_pool);

  block_pool->bp_blocks.dbi =
    spn_vk_ds_get_block_pool_bp_blocks(instance, block_pool->ds_block_pool);

  block_pool->bp_host_map.dbi =
    spn_vk_ds_get_block_pool_bp_host_map(instance, block_pool->ds_block_pool);

#ifdef SPN_BP_DEBUG
  block_pool->bp_debug.d.dbi =
    spn_vk_ds_get_block_pool_bp_debug(instance, block_pool->ds_block_pool);

  size_t const bp_debug_size =
    SPN_VK_BUFFER_OFFSETOF(block_pool, bp_debug, bp_debug) + SPN_BP_DEBUG_SIZE;

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                  &device->environment,
                                  bp_debug_size,
                                  NULL,
                                  block_pool->bp_debug.d.dbi,
                                  &block_pool->bp_debug.d.dm);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.copyback,
                                  &device->environment,
                                  bp_debug_size,
                                  NULL,
                                  &block_pool->bp_debug.h.dbi,
                                  &block_pool->bp_debug.h.dm);

  vk(MapMemory(device->environment.d,
               block_pool->bp_debug.h.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&block_pool->bp_debug.h.mapped));
#endif

  // allocate buffers
  size_t const bp_ids_size =
    SPN_VK_BUFFER_OFFSETOF(block_pool, bp_ids, bp_ids) + id_count * sizeof(spn_block_id_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                  &device->environment,
                                  bp_ids_size,
                                  NULL,
                                  block_pool->bp_ids.dbi,
                                  &block_pool->bp_ids.dm);

  uint32_t const bp_dwords = block_count * block_dwords;
  size_t const   bp_blocks_size =
    SPN_VK_BUFFER_OFFSETOF(block_pool, bp_blocks, bp_blocks) + bp_dwords * sizeof(uint32_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                  &device->environment,
                                  bp_blocks_size,
                                  NULL,
                                  block_pool->bp_blocks.dbi,
                                  &block_pool->bp_blocks.dm);

  size_t const bp_host_map_size = SPN_VK_BUFFER_OFFSETOF(block_pool, bp_host_map, bp_host_map) +
                                  handle_count * sizeof(spn_handle_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                  &device->environment,
                                  bp_host_map_size,
                                  NULL,
                                  block_pool->bp_host_map.dbi,
                                  &block_pool->bp_host_map.dm);

  // update the block pool ds
  spn_vk_ds_update_block_pool(instance, &device->environment, block_pool->ds_block_pool);

  //
  // initialize the block pool
  //
  spn_dispatch_id_t id;

  spn(device_dispatch_acquire(device, SPN_DISPATCH_STAGE_BLOCK_POOL, &id));

  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, id);

#ifdef SPN_BP_DEBUG
  vkCmdFillBuffer(cb, block_pool->bp_debug.d.dbi->buffer, 0, sizeof(uint32_t), 0);

  vk_barrier_transfer_w_to_compute_r(cb);
#endif

  // bind the global block pool
  spn_vk_ds_bind_block_pool_init_block_pool(instance, cb, block_pool->ds_block_pool);

  // append push constants
  struct spn_vk_push_block_pool_init const push = { .bp_size = block_pool->bp_size };

  spn_vk_p_push_block_pool_init(instance, cb, &push);

  // bind pipeline
  spn_vk_p_bind_block_pool_init(instance, cb);

  // size the grid
  uint32_t const wg_ids =
    config->p.group_sizes.named.block_pool_init.workgroup * config->block_pool.ids_per_invocation;

  uint32_t const wgs = (block_pool->bp_size + wg_ids - 1) / wg_ids;

  // dispatch the pipeline
  vkCmdDispatch(cb, wgs, 1, 1);

  spn_device_dispatch_submit(device, id);

  //
  // FIXME(allanmac): we could continue intializing and drain the device
  // as late as possible.
  //
  spn(device_wait_all(device, true));
}

//
//
//

void
spn_device_block_pool_dispose(struct spn_device * const device)
{
  struct spn_vk * const         instance   = device->instance;
  struct spn_block_pool * const block_pool = device->block_pool;

  spn_vk_ds_release_block_pool(instance, block_pool->ds_block_pool);

#ifdef SPN_BP_DEBUG
  spn_allocator_device_perm_free(&device->allocator.device.perm.copyback,
                                 &device->environment,
                                 &block_pool->bp_debug.h.dbi,
                                 block_pool->bp_debug.h.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                 &device->environment,
                                 block_pool->bp_debug.d.dbi,
                                 block_pool->bp_debug.d.dm);
#endif

  spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                 &device->environment,
                                 block_pool->bp_host_map.dbi,
                                 block_pool->bp_host_map.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                 &device->environment,
                                 block_pool->bp_blocks.dbi,
                                 block_pool->bp_blocks.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                 &device->environment,
                                 block_pool->bp_ids.dbi,
                                 block_pool->bp_ids.dm);

  spn_allocator_host_perm_free(&device->allocator.host.perm, device->block_pool);
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

struct spn_vk_ds_block_pool_t
spn_device_block_pool_get_ds(struct spn_device * const device)
{
  return device->block_pool->ds_block_pool;
}

//
//
//

uint32_t
spn_device_block_pool_get_size(struct spn_device * const device)
{
  return device->block_pool->bp_size;
}

//
//
//
