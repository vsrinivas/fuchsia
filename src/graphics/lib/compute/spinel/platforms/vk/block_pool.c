// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block_pool.h"

#include <stdlib.h>

#include "common/vk/assert.h"
#include "common/vk/barrier.h"
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
}

void
spn_device_block_pool_debug_print(struct spn_device * const device)
{
  struct spn_vk_target_config const * const config = spn_vk_get_config(device->instance);

  if ((config->allocator.device.hr_dw.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
      VkMappedMemoryRange const mmr = { .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                        .pNext  = NULL,
                                        .memory = device->block_pool->bp_debug.h.dm,
                                        .offset = 0,
                                        .size   = VK_WHOLE_SIZE };

      vk(InvalidateMappedMemoryRanges(device->environment.d, 1, &mmr));
    }

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
    float const * bp_debug_float = (float *)mapped->bp_debug;

    for (uint32_t ii = 0; ii < count; ii += 4)
      {
        fprintf(stderr,
                "{ { %10.2f, %10.2f }, { %10.2f, %10.2f } },\n",
                bp_debug_float[ii + 0],
                bp_debug_float[ii + 1],
                bp_debug_float[ii + 2],
                bp_debug_float[ii + 3]);
      }
  }
#endif

  //
  // TTS
  //
#if 0
  {
    union bp_xy
    {
      uint32_t dword;

      struct
      {
        uint32_t x : SPN_TTRK_LO_BITS_X + SPN_TTRK_HI_BITS_X;
        uint32_t y : SPN_TTRK_HI_BITS_Y;
      };
    };

    union bp_tts
    {
      uint32_t dword;

      struct
      {
        uint32_t tx : SPN_TTS_BITS_TX;
        int32_t  dx : SPN_TTS_BITS_DX;
        uint32_t ty : SPN_TTS_BITS_TY;
        int32_t  dy : SPN_TTS_BITS_DY;
      };
    };

    uint32_t const subgroup_size = (1 << config->p.group_sizes.named.rasterize_line.subgroup_log2);
    uint32_t const loop_size     = 16 + 7 * subgroup_size;

    for (uint32_t ii = 0; ii < count; ii += loop_size)
      {
        uint32_t jj = ii;

        fprintf(stderr, "====================================\n");

        for (uint32_t kk = 0; kk < 16; kk++)
          {
            fprintf(stderr, "%10u ", mapped->bp_debug[jj++]);
          }

        fprintf(stderr, "\n====================================\n");

        for (uint32_t kk = 0; kk < subgroup_size; kk++)
          {
            fprintf(stderr, "(* %10u *) ", mapped->bp_debug[jj + 2]);

            union bp_tts const tts = { mapped->bp_debug[jj + 1] };

            if (tts.dword != SPN_TTS_INVALID)
              {
                union bp_xy const xy = { mapped->bp_debug[jj + 0] };

                uint32_t const tile_x = xy.x << (config->tile.width_log2 + SPN_TTS_SUBPIXEL_X_LOG2);
                uint32_t const tile_y = xy.y
                                        << (config->tile.height_log2 + SPN_TTS_SUBPIXEL_Y_LOG2);

                uint32_t const dx_abs = abs(tts.dx);
                uint32_t const x_lo   = tile_x + tts.tx;
                uint32_t const x_hi   = x_lo + dx_abs;

                int32_t const  dy     = tts.dy + (tts.dy >= 0 ? 1 : 0);
                uint32_t const dy_abs = abs(dy);
                uint32_t const y_lo   = tile_y + tts.ty;
                uint32_t const y_hi   = y_lo + dy_abs;

                fprintf(
                  stderr,
                  "(* %10u : %10u : %10u : %10u *) (* %10u : ( %10u, %10u ) *) { { %10u.0, %10u.0 }, { %10u.0, %10u.0 } },\n",
                  mapped->bp_debug[jj + 3],  // part_idx
                  mapped->bp_debug[jj + 4],  // part_msb
                  mapped->bp_debug[jj + 5],  // entry_xy
                  mapped->bp_debug[jj + 6],  // entry_base
                  xy.dword,                  // xy
                  xy.x,                      // x
                  xy.y,                      // y
                  tts.dx >= 0 ? x_lo : x_hi,
                  dy > 0 ? y_lo : y_hi,
                  tts.dx >= 0 ? x_hi : x_lo,
                  dy > 0 ? y_hi : y_lo);
              }
            else
              {
                fprintf(stderr, "*** SPN_TTS_INVALID ***\n");
              }

            jj += 7;
          }
      }
  }
#endif

  //
  // TTRK
  //
#if 0
  {
    union bp_ttrk
    {
      struct
      {g
        uint32_t dword_lo;
        uint32_t dword_hi;
      };

      struct
      {
        // clang-format off
        uint64_t ttsb_id : SPN_TTRK_LO_BITS_TTSB_ID;
        uint64_t new_x   : 1;
        uint64_t new_y   : 1;
        uint64_t x       : SPN_TTRK_LO_BITS_X + SPN_TTRK_HI_BITS_X;
        uint64_t y       : SPN_TTRK_HI_BITS_Y;
        uint64_t cohort  : SPN_TTRK_HI_BITS_COHORT;
        // clang-format on
      };
    };

    for (uint32_t ii = 0; ii < count; ii += 2)
      {
        union bp_ttrk const ttrk = { .dword_lo = mapped->bp_debug[ii + 0],
                                     .dword_hi = mapped->bp_debug[ii + 1] };

        if ((ttrk.dword_lo == 0xFFFFFFFF) && (ttrk.dword_hi == 0xFFFFFFFF))
          continue;

        uint32_t const w = 1 << (config->tile.width_log2 + SPN_TTS_SUBPIXEL_X_LOG2);
        uint32_t const h = 1 << (config->tile.height_log2 + SPN_TTS_SUBPIXEL_Y_LOG2);
        uint32_t const x = ttrk.x * w;
        uint32_t const y = ttrk.y * h;

        // clang-format off
        fprintf(stderr,
                "(* %s *)\n"
                "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n"
                "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n"
                "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n"
                "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n",
                ttrk.new_x ? "X" : (ttrk.new_y ? "Y" : "-"),
                x  , y  ,x+w,y  ,
                x+w, y  ,x+w,y+h,
                x+w, y+h,x  ,y+h,
                x  , y+h,x  ,y  );
        // clang-format on
      }
  }
#endif

  //
  // TTXK
  //
#if 0
  {
    union bp_ttxk
    {
      struct
      {
        uint32_t dword_lo;
        uint32_t dword_hi;
      };

      struct
      {
        // clang-format off
        uint64_t ttpb_id : SPN_TTXK_LO_BITS_TTXB_ID;
        uint64_t span    : SPN_TTXK_LO_HI_BITS_SPAN;
        uint64_t x       : SPN_TTXK_HI_BITS_X;
        uint64_t y       : SPN_TTXK_HI_BITS_Y;
        // clang-format on
      };
    };

    for (uint32_t ii = 0; ii < count; ii += 2)
      {
        union bp_ttxk const ttxk = { .dword_lo = mapped->bp_debug[ii + 0],
                                     .dword_hi = mapped->bp_debug[ii + 1] };

        uint32_t const w    = 1 << (config->tile.width_log2 + SPN_TTS_SUBPIXEL_X_LOG2);
        uint32_t const h    = 1 << (config->tile.height_log2 + SPN_TTS_SUBPIXEL_Y_LOG2);
        uint32_t const span = w * ttxk.span;
        uint32_t const x    = ttxk.x * w;
        uint32_t const y    = ttxk.y * h;

        // clang-format off
        if (span != 0)
        {
          fprintf(stderr,
                  "(* %u *)\n"
                  "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n"
                  "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n"
                  "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n"
                  "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n",
                  ttxk.span,
                  x     , y  , x+span, y  ,
                  x+span, y  , x+span, y+h,
                  x+span, y+h, x     , y+h,
                  x     , y+h, x     , y  );
        }
        else // (span == 0)
        {
          fprintf(stderr,
                  "(* %u *)\n"
                  "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n"
                  "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n"
                  "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n"
                  "{ { %u.0, %u.0 }, { %u.0, %u.0 } },\n",
                  ttxk.span,
                  x  , y  , x+w, y+h,
                  x  , y  , x+w, y+h,
                  x  , y+h, x+w, y  ,
                  x  , y+h, x+w, y);
        }
        // clang-format on
      }
  }
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

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.drw,
                                  &device->environment,
                                  bp_debug_size,
                                  NULL,
                                  block_pool->bp_debug.d.dbi,
                                  &block_pool->bp_debug.d.dm);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.hr_dw,
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

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.drw,
                                  &device->environment,
                                  bp_ids_size,
                                  NULL,
                                  block_pool->bp_ids.dbi,
                                  &block_pool->bp_ids.dm);

  uint32_t const bp_dwords = block_count * block_dwords;
  size_t const   bp_blocks_size =
    SPN_VK_BUFFER_OFFSETOF(block_pool, bp_blocks, bp_blocks) + bp_dwords * sizeof(uint32_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.drw,
                                  &device->environment,
                                  bp_blocks_size,
                                  NULL,
                                  block_pool->bp_blocks.dbi,
                                  &block_pool->bp_blocks.dm);

  size_t const bp_host_map_size = SPN_VK_BUFFER_OFFSETOF(block_pool, bp_host_map, bp_host_map) +
                                  handle_count * sizeof(spn_handle_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.drw,
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
  spn(device_wait_all(device, true, __func__));
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
  spn_allocator_device_perm_free(&device->allocator.device.perm.hr_dw,
                                 &device->environment,
                                 &block_pool->bp_debug.h.dbi,
                                 block_pool->bp_debug.h.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.drw,
                                 &device->environment,
                                 block_pool->bp_debug.d.dbi,
                                 block_pool->bp_debug.d.dm);
#endif

  spn_allocator_device_perm_free(&device->allocator.device.perm.drw,
                                 &device->environment,
                                 block_pool->bp_host_map.dbi,
                                 block_pool->bp_host_map.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.drw,
                                 &device->environment,
                                 block_pool->bp_blocks.dbi,
                                 block_pool->bp_blocks.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.drw,
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
