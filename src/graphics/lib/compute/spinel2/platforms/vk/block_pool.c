// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block_pool.h"

#include <assert.h>
#include <stdlib.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "core_c.h"
#include "deps.h"
#include "device.h"
#include "handle_pool.h"
#include "shaders/push.h"
#include "spinel/spinel_assert.h"

//
// FIXME(allanmac): The same routine is found in common/utils
//
static uint32_t
spinel_pow2_ru_u32(uint32_t n)
{
  assert(n <= 0x80000000U);

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
spinel_device_block_pool_dispose(struct spinel_device * device)
{
  struct spinel_block_pool * const block_pool = &device->block_pool;

  // All dbi structures share the same VkBuffer
  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                               device->vk.d,
                               device->vk.ac,
                               &block_pool->vk.dbi_dm.bp);
}

//
// Record the block pool initialization commands.
//
static VkPipelineStageFlags
spinel_block_pool_init_record(VkCommandBuffer cb, void * data0, void * data1)
{
  struct spinel_device * const device     = data0;
  struct spinel_block_pool *   block_pool = &device->block_pool;

  //
  // Record
  //
  struct spinel_push_block_pool_init const push = {

    .devaddr_block_pool_ids = block_pool->vk.dbi_devaddr.ids.devaddr,
    .bp_size                = block_pool->bp_size
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.block_pool_init,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push),
                     &push);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.block_pool_init);

  //
  // Size the dispatch
  //
  struct spinel_target_config const * config = &device->ti.config;

  // ids per workgroup
  uint32_t const bpis_per_wg = config->group_sizes.named.block_pool_init.workgroup *  //
                               config->block_pool.ids_per_invocation;

  // round up
  uint32_t const bpi_wgs = (block_pool->bp_size + bpis_per_wg - 1) / bpis_per_wg;

  vkCmdDispatch(cb, bpi_wgs, 1, 1);

  //
  // This command buffer ends with a compute shader
  //
  return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
}

//
// Initialize the block pool
//
static void
spinel_device_block_pool_init(struct spinel_device * device)
{
  struct spinel_deps_immediate_submit_info const disi = {
    .record = {
      .pfn   = spinel_block_pool_init_record,
      .data0 = device,
    },
    //
    // The rest of submit_info structure is zeroed -- these are the defaults we
    // want for this submission.
    //
  };

  //
  // NOTE: We don't need to save the immediate semaphore handle because context
  // creation will block and drain all submissions before returning.
  //
  spinel_deps_immediate_submit(device->deps, &device->vk, &disi, NULL);
}

//
//
//
void
spinel_device_block_pool_create(struct spinel_device * device,
                                uint64_t               block_pool_size,
                                uint32_t               handle_count)
{
  struct spinel_target_config const * const config = &device->ti.config;

  // How large is this target's block?
  uint32_t const block_dwords_log2 = config->block_pool.block_dwords_log2;
  uint32_t const block_dwords      = 1 << block_dwords_log2;

  // How large is the block pool?
  uint64_t const block_pool_size_ru = block_pool_size + sizeof(uint32_t) - 1;
  uint32_t const block_pool_dwords  = (uint32_t)(block_pool_size_ru / sizeof(uint32_t));
  uint32_t const block_count        = (block_pool_dwords + block_dwords - 1) >> block_dwords_log2;

  // The `bp_ids` extent is always a power-of-two.
  uint32_t const id_count_pow2 = spinel_pow2_ru_u32(block_count);

  struct spinel_block_pool * const block_pool = &device->block_pool;

  block_pool->bp_size = block_count;
  block_pool->bp_mask = id_count_pow2 - 1;  // ids ring is power-of-two

  //
  // Allocate in one buffer:
  //
  //   [ `bp_blocks` | `bp_ids` | `bp_host_map` ]
  //
  // Note that `bp_blocks` is first in order to robustify alignment.
  //
  // Even though we're using device addresses inside of Spinel, we keep the
  // intermediate VkDescriptorBufferInfo structures around in case we need to
  // interfact with descriptor sets.
  //
  // clang-format off
  uint32_t     const bp_dwords         = block_count * block_dwords;
  VkDeviceSize const bp_blocks_size    = bp_dwords * sizeof(uint32_t);
  VkDeviceSize const bp_ids_offset_ids = SPN_BUFFER_OFFSETOF(block_pool_ids, ids);
  VkDeviceSize const bp_ids_size       = bp_ids_offset_ids + id_count_pow2 * sizeof(spinel_block_id_t);
  VkDeviceSize const bp_host_map_size  = handle_count * sizeof(spinel_handle_t);
  VkDeviceSize const bp_size           = bp_blocks_size + bp_ids_size + bp_host_map_size;
  // clang-format on

  spinel_allocator_alloc_dbi_dm(&device->allocator.device.perm.drw,
                                device->vk.pd,
                                device->vk.d,
                                device->vk.ac,
                                bp_size,
                                NULL,
                                &block_pool->vk.dbi_dm.bp);

  //
  // Init dbi_devaddr structs
  //
  spinel_dbi_devaddr_from_dbi(device->vk.d,
                              &block_pool->vk.dbi_devaddr.blocks,
                              &block_pool->vk.dbi_dm.bp.dbi,
                              0UL,
                              bp_blocks_size);

  spinel_dbi_devaddr_from_dbi(device->vk.d,
                              &block_pool->vk.dbi_devaddr.ids,
                              &block_pool->vk.dbi_dm.bp.dbi,
                              bp_blocks_size,
                              bp_ids_size);

  spinel_dbi_devaddr_from_dbi(device->vk.d,
                              &block_pool->vk.dbi_devaddr.host_map,
                              &block_pool->vk.dbi_dm.bp.dbi,
                              bp_blocks_size + bp_ids_size,
                              bp_host_map_size);

  //
  // Initialize and wait for completion elsewhere before the new context is
  // returned.
  //
  spinel_device_block_pool_init(device);
}

//
//
//
