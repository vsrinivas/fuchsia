// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "composition_impl.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block_pool.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "device.h"
#include "handle_pool.h"
#include "queue_pool.h"
#include "raster_builder_impl.h"
#include "ring.h"
#include "shaders/push.h"
#include "spinel/spinel_assert.h"
#include "state_assert.h"

//
// The composition launches a number of dependent command buffers:
//
//   1. RESET TTCK atomic count
//   2. PLACE shaders
//   3. SORT INDIRECT keyvals
//   4. SEGMENT keyvals
//

//
// Per-dispatch state
//
struct spinel_ci_dispatch
{
  struct
  {
    uint32_t head;
    uint32_t span;
  } cp;  // place commands

  struct
  {
    uint32_t head;
  } rd;  // raster handles are 1:1 with place commands

  struct
  {
    spinel_deps_immediate_semaphore_t immediate;  // "invalid" once drained
  } signal;
};

//
// Vulkan objects
//
struct spinel_ci_vk
{
  struct
  {
    struct spinel_dbi_dm_devaddr h;
    struct spinel_dbi_dm_devaddr d;
  } rings;

  struct spinel_dbi_dm_devaddr ttcks;
  struct spinel_dbi_dm         ttck_keyvals_odd;
  struct spinel_dbi_devaddr    ttck_keyvals_out;

  struct
  {
    struct spinel_dbi_dm internal;
    struct spinel_dbi_dm indirect;
  } rs;
};

//
// Valid states
//
typedef enum spinel_ci_state_e
{
  SPN_CI_STATE_RESETTING,  // unsealed and resetting
  SPN_CI_STATE_UNSEALED,   // unsealed and ready to place rasters
  SPN_CI_STATE_SEALING,    // waiting for PLACE and TTCK_SORT
  SPN_CI_STATE_SEALED      // sort & segment complete

} spinel_ci_state_e;

//
//
//
struct spinel_composition_impl
{
  struct spinel_composition * composition;
  struct spinel_device *      device;

  //
  // Vulkan resources
  //
  struct spinel_ci_vk vk;

  //
  // composition clip
  //
  SPN_TYPE_I32VEC4 clip;

  //
  // host mapped command ring and copyback counts
  //
  struct
  {
    struct
    {
      struct spinel_cmd_place * extent;
      struct spinel_ring        ring;
    } cp;  // place commands
  } mapped;

  //
  // records of work-in-progress and work-in-flight
  //
  struct
  {
    struct spinel_ci_dispatch * extent;
    struct spinel_ring          ring;
  } dispatches;

  //
  // all rasters are retained until reset or release
  //
  struct
  {
    spinel_handle_t * extent;
    uint32_t          size;
    uint32_t          count;
  } rasters;

  uint32_t          lock_count;  // # of wip renders
  spinel_ci_state_e state;       // state of composition

  //
  // signalling timelines
  //
  struct
  {
    struct
    {
      spinel_deps_immediate_semaphore_t immediate;
    } resetting;
    struct
    {
      spinel_deps_immediate_semaphore_t immediate;
    } sealing;
  } signal;
};

//
//
//
static bool
spinel_ci_is_staged(struct spinel_target_config const * config)
{
  return ((config->allocator.device.hw_dr.properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0);
}

//
// A dispatch captures how many paths and blocks are in a dispatched or
// the work-in-progress compute grid.
//
static struct spinel_ci_dispatch *
spinel_ci_dispatch_head(struct spinel_composition_impl * impl)
{
  assert(!spinel_ring_is_empty(&impl->dispatches.ring));

  return impl->dispatches.extent + impl->dispatches.ring.head;
}

static struct spinel_ci_dispatch *
spinel_ci_dispatch_tail(struct spinel_composition_impl * impl)
{
  assert(!spinel_ring_is_full(&impl->dispatches.ring));

  return impl->dispatches.extent + impl->dispatches.ring.tail;
}

static bool
spinel_ci_dispatch_is_empty(struct spinel_ci_dispatch const * dispatch)
{
  return (dispatch->cp.span == 0);
}

static void
spinel_ci_dispatch_init(struct spinel_composition_impl * impl, struct spinel_ci_dispatch * dispatch)
{
  // .signal doesn't need initialization
  *dispatch = (struct spinel_ci_dispatch){

    .cp = { .head = impl->mapped.cp.ring.head,  //
            .span = 0 },
    .rd = { .head = impl->rasters.count }
  };
}

static void
spinel_ci_dispatch_drop(struct spinel_composition_impl * impl)
{
  struct spinel_ring * const ring = &impl->dispatches.ring;

  spinel_ring_drop_1(ring);
}

static void
spinel_ci_dispatch_acquire(struct spinel_composition_impl * impl)
{
  struct spinel_ring * const   ring   = &impl->dispatches.ring;
  struct spinel_device * const device = impl->device;

  while (spinel_ring_is_empty(ring))
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  struct spinel_ci_dispatch * const dispatch = spinel_ci_dispatch_head(impl);

  spinel_ci_dispatch_init(impl, dispatch);
}

//
//
//
static void
spinel_ci_place_flush_complete(void * data0, void * data1)
{
  struct spinel_composition_impl * impl     = data0;
  struct spinel_ci_dispatch *      dispatch = data1;

  //
  // If the dispatch is the tail of the ring then try to release as
  // many dispatch records as possible...
  //
  // Note that kernels can complete in any order so the release
  // records need to add to the mapped.ring.tail in order.
  //
  dispatch->signal.immediate = SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID;

  dispatch = spinel_ci_dispatch_tail(impl);

  while (dispatch->signal.immediate == SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID)
    {
      // release ring span
      spinel_ring_release_n(&impl->mapped.cp.ring, dispatch->cp.span);

      // release the dispatch
      spinel_ring_release_n(&impl->dispatches.ring, 1);

      // any dispatches in flight?
      if (spinel_ring_is_full(&impl->dispatches.ring))
        {
          break;
        }

      // get new tail
      dispatch = spinel_ci_dispatch_tail(impl);
    }
}

//
//
//
static VkPipelineStageFlags
spinel_ci_place_flush_record(VkCommandBuffer cb, void * data0, void * data1)
{
  struct spinel_composition_impl * const    impl     = data0;
  struct spinel_device * const              device   = impl->device;
  struct spinel_target_config const * const config   = &device->ti.config;
  struct spinel_ci_dispatch * const         dispatch = data1;

  if (spinel_ci_is_staged(config))
    {
      VkDeviceSize const head_offset = dispatch->cp.head * sizeof(struct spinel_cmd_place);

      if (dispatch->cp.head + dispatch->cp.span <= impl->mapped.cp.ring.size)
        {
          VkBufferCopy bcs[1];

          bcs[0].srcOffset = impl->vk.rings.h.dbi_dm.dbi.offset + head_offset;
          bcs[0].dstOffset = impl->vk.rings.d.dbi_dm.dbi.offset + head_offset;
          bcs[0].size      = dispatch->cp.span * sizeof(struct spinel_cmd_place);

          vkCmdCopyBuffer(cb,
                          impl->vk.rings.h.dbi_dm.dbi.buffer,
                          impl->vk.rings.d.dbi_dm.dbi.buffer,
                          1,
                          bcs);
        }
      else  // wraps around ring
        {
          VkBufferCopy bcs[2];

          uint32_t const hi = impl->mapped.cp.ring.size - dispatch->cp.head;
          bcs[0].srcOffset  = impl->vk.rings.h.dbi_dm.dbi.offset + head_offset;
          bcs[0].dstOffset  = impl->vk.rings.d.dbi_dm.dbi.offset + head_offset;
          bcs[0].size       = hi * sizeof(struct spinel_cmd_place);

          uint32_t const lo = dispatch->cp.head + dispatch->cp.span - impl->mapped.cp.ring.size;
          bcs[1].srcOffset  = impl->vk.rings.h.dbi_dm.dbi.offset;
          bcs[1].dstOffset  = impl->vk.rings.d.dbi_dm.dbi.offset;
          bcs[1].size       = lo * sizeof(struct spinel_cmd_place);

          vkCmdCopyBuffer(cb,
                          impl->vk.rings.h.dbi_dm.dbi.buffer,
                          impl->vk.rings.d.dbi_dm.dbi.buffer,
                          2,
                          bcs);
        }

      vk_barrier_transfer_w_to_compute_r(cb);
    }

  //
  // PLACE
  //
  // NOTE(allanmac): PLACE_TTPK and PLACE_TTSK have compatible push constants.
  //
  struct spinel_push_place const push_place = {

    .place_clip                  = impl->clip,
    .devaddr_block_pool_blocks   = device->block_pool.vk.dbi_devaddr.blocks.devaddr,
    .devaddr_block_pool_host_map = device->block_pool.vk.dbi_devaddr.host_map.devaddr,
    .devaddr_ttcks               = impl->vk.ttcks.devaddr,
    .devaddr_place               = impl->vk.rings.d.devaddr,
    .place_head                  = dispatch->cp.head,
    .place_span                  = dispatch->cp.span,
    .place_size                  = impl->mapped.cp.ring.size
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.place_ttpk,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_place),
                     &push_place);

  //
  // dispatch one subgroup per command -- place_ttpk and place_ttsk are same
  //
  // clang-format off
  uint32_t const place_wg_size      = config->group_sizes.named.place_ttpk.workgroup;
  uint32_t const place_sg_size_log2 = config->group_sizes.named.place_ttpk.subgroup_log2;
  uint32_t const place_cmds_per_wg  = place_wg_size >> place_sg_size_log2;
  uint32_t const place_wgs          = (dispatch->cp.span + place_cmds_per_wg - 1) / place_cmds_per_wg;
  // clang-format on

  // bind PLACE_TTPK
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.place_ttpk);

  // dispatch PLACE_TTPK
  vkCmdDispatch(cb, place_wgs, 1, 1);

  // bind PLACE_TTSK
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.place_ttsk);

  // dispatch PLACE_TTSK
  vkCmdDispatch(cb, place_wgs, 1, 1);

  //
  // This command buffer ends with a compute shader
  //
  return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
}

//
//
//
static void
spinel_ci_place_flush(struct spinel_composition_impl * impl)
{
  struct spinel_ci_dispatch * const dispatch = spinel_ci_dispatch_head(impl);

  // Is this a dispatch with no commands?
  if (spinel_ci_dispatch_is_empty(dispatch))
    {
      return;
    }

  //
  // Submit dispatch
  //
  // Waits upon:
  //
  //  * Composition reset
  //  * Materialization of raster handles
  //
  bool const is_resetting = (impl->signal.resetting.immediate !=  //
                             SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID);

  struct spinel_deps_immediate_submit_info const disi = {
    .record = {
      .pfn   = spinel_ci_place_flush_record,
      .data0 = impl,
      .data1 = dispatch,
    },
    .wait = {
      .immediate = {
        .count      = (is_resetting ? 1 : 0),
        .semaphores = { impl->signal.resetting.immediate },
      },
      .delayed = {
        .handles = {
          .extent = impl->rasters.extent,
          .size   = impl->rasters.size,
          .head   = dispatch->rd.head,
          .span   = dispatch->cp.span,
        },
      },
    },
    .completion = {
      .pfn   = spinel_ci_place_flush_complete,
      .data0 = impl,
      .data1 = dispatch,
    },
  };

  //
  // The current dispatch is now sealed so drop it
  //
  spinel_ci_dispatch_drop(impl);

  //
  // Submit!
  //
  struct spinel_device * const device = impl->device;

  spinel_deps_immediate_submit(device->deps, &device->vk, &disi, &dispatch->signal.immediate);

  //
  // Acquire and initialize the next dispatch
  //
  spinel_ci_dispatch_acquire(impl);
}

//
// COMPLETION: SEALING
//
//   PHASE 1: COPYBACK5
//   PHASE 2: SORT & SEGMENT
//
// The same payload is used for both phases
//
static void
spinel_ci_unsealed_to_sealed_complete(void * data0, void * data1)
{
  struct spinel_composition_impl * const impl = data0;

  impl->state = SPN_CI_STATE_SEALED;

  impl->signal.sealing.immediate = SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID;
}

//
//
//
static VkPipelineStageFlags
spinel_ci_unsealed_to_sealed_record(VkCommandBuffer cb, void * data0, void * data1)
{
  struct spinel_composition_impl * impl   = data0;
  struct spinel_device * const     device = impl->device;

  //
  // Sort the TTCK keyvals
  //
  VkDescriptorBufferInfo const ttck_count_dbi = {

    .buffer = impl->vk.ttcks.dbi_dm.dbi.buffer,
    .offset = impl->vk.ttcks.dbi_dm.dbi.offset + SPN_BUFFER_OFFSETOF(ttcks, segment_dispatch.w),
    .range  = sizeof(uint32_t)
  };

  VkDescriptorBufferInfo const ttck_keyvals_even_dbi = {

    .buffer = impl->vk.ttcks.dbi_dm.dbi.buffer,
    .offset = impl->vk.ttcks.dbi_dm.dbi.offset + SPN_BUFFER_OFFSETOF(ttcks, ttck_keyvals),
    .range  = impl->vk.ttcks.dbi_dm.dbi.range - SPN_BUFFER_OFFSETOF(ttcks, ttck_keyvals)
  };

  struct radix_sort_vk_sort_indirect_info const info = {

    .ext          = NULL,
    .key_bits     = SPN_TTCK_HI_BITS_LXY,
    .count        = &ttck_count_dbi,
    .keyvals_even = &ttck_keyvals_even_dbi,
    .keyvals_odd  = &impl->vk.ttck_keyvals_odd.dbi,
    .internal     = &impl->vk.rs.internal.dbi,
    .indirect     = &impl->vk.rs.indirect.dbi
  };

  radix_sort_vk_sort_indirect(device->ti.rs,
                              &info,
                              device->vk.d,
                              cb,
                              &impl->vk.ttck_keyvals_out.dbi);

  //
  // Init ttck_keyvals_out.devaddr
  //
  spinel_dbi_devaddr_init_devaddr(impl->device->vk.d, &impl->vk.ttck_keyvals_out);

  //
  // COMPUTE>COMPUTE
  //
  vk_barrier_compute_w_to_compute_r(cb);

  //
  // Dispatch TTCKS_SEGMENT_DISPATCH
  //
  struct spinel_push_ttcks_segment_dispatch const push_ttcks_segment_dispatch = {

    .devaddr_ttcks_header = impl->vk.ttcks.devaddr,
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.ttcks_segment_dispatch,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_ttcks_segment_dispatch),
                     &push_ttcks_segment_dispatch);

  vkCmdBindPipeline(cb,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    device->ti.pipelines.named.ttcks_segment_dispatch);

  vkCmdDispatch(cb, 1, 1, 1);

  //
  // COMPUTE>INDIRECT|COMPUTE
  //
  vk_barrier_compute_w_to_indirect_compute_r(cb);

  //
  // Dispatch TTCKS_SEGMENT
  //
  struct spinel_push_ttcks_segment const push_ttcks_segment = {

    .devaddr_ttcks_header = impl->vk.ttcks.devaddr,
    .devaddr_ttck_keyvals = impl->vk.ttck_keyvals_out.devaddr
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.ttcks_segment,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_ttcks_segment),
                     &push_ttcks_segment);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.ttcks_segment);

  //
  // Dispatch segmentation pipeline indirectly
  //
  VkDeviceSize const ttcks_segment_dispatch_offset = SPN_BUFFER_OFFSETOF(ttcks, segment_dispatch);

  vkCmdDispatchIndirect(cb, impl->vk.ttcks.dbi_dm.dbi.buffer, ttcks_segment_dispatch_offset);

  //
  // This command buffer ends with a compute shader
  //
  return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
}

//
// Accumulate wait.immediate semaphores:
//
//   1. Resetting signalling timeline
//   2. All in-flight "PLACE" signalling timelines
//
static void
spinel_ci_unsealed_to_sealed_accumulate_waits(struct spinel_composition_impl *           impl,
                                              struct spinel_deps_immediate_submit_info * disi)
{
  //
  // We also wait on resetting here in case there were zero dispatches
  //
  bool const is_resetting = (impl->signal.resetting.immediate !=  //
                             SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID);

  if (is_resetting)
    {
      disi->wait.immediate.semaphores[disi->wait.immediate.count++] =
        impl->signal.resetting.immediate;
    }

  //
  // In-flight dispatches
  //
  struct spinel_ring const * const        ring       = &impl->dispatches.ring;
  uint32_t const                          in_flight  = spinel_ring_dropped(ring);
  uint32_t                                tail       = ring->tail;
  struct spinel_ci_dispatch const * const dispatches = impl->dispatches.extent;

  for (uint32_t ii = 0; ii < in_flight; ii++)
    {
      struct spinel_ci_dispatch const * const dispatch = dispatches + tail++;

      if (dispatch->signal.immediate != SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID)
        {
          disi->wait.immediate.semaphores[disi->wait.immediate.count++] =
            dispatch->signal.immediate;
        }

      if (tail == ring->size)
        {
          tail = 0;
        }
    }
}

//
// 1. Flush work-in-progress place dispatch
// 2. Indirect radix sort
// 3. Segment ttck keys
//
static void
spinel_ci_unsealed_to_sealed(struct spinel_composition_impl * impl)
{
  //
  // Move to SEALING state...
  //
  impl->state = SPN_CI_STATE_SEALING;

  //
  // Flush any work-in-progress place
  //
  spinel_ci_place_flush(impl);

  //
  // Prepare the deps submit info
  //
  struct spinel_device * const device = impl->device;

  struct spinel_deps_immediate_submit_info disi = {
    .record = {
      .pfn   = spinel_ci_unsealed_to_sealed_record,
      .data0 = impl,
    },
    .completion = {
      .pfn   = spinel_ci_unsealed_to_sealed_complete,  //
      .data0 = impl,
    },
  };

  //
  // Gather place dependencies
  //
  spinel_ci_unsealed_to_sealed_accumulate_waits(impl, &disi);

  //
  // Acquire an immediate semaphore
  //
  spinel_deps_immediate_submit(device->deps, &device->vk, &disi, &impl->signal.sealing.immediate);
}

//
//
//
static void
spinel_ci_unsealed_reset_complete(void * data0, void * data1)
{
  struct spinel_composition_impl * const impl = data0;

  //
  // move to UNSEALED state and invalidate timeline
  //
  impl->state                      = SPN_CI_STATE_UNSEALED;
  impl->signal.resetting.immediate = SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID;

  //
  // are there retained rasters?
  //
  if (impl->rasters.count > 0)
    {
      //
      // release retained rasters
      //
      spinel_device_release_d_rasters(impl->device, impl->rasters.extent, impl->rasters.count);

      //
      // zero the count
      //
      impl->rasters.count = 0;

      //
      // reset the WIP dispatch
      //
      struct spinel_ci_dispatch * const dispatch = spinel_ci_dispatch_head(impl);

      spinel_ci_dispatch_init(impl, dispatch);
    }
}

//
// Zero `.ttcks_count` and `.offset_count`
//
static VkPipelineStageFlags
spinel_ci_unsealed_reset_record(VkCommandBuffer cb, void * data0, void * data1)
{
  struct spinel_composition_impl * const impl = data0;

  vkCmdFillBuffer(cb,
                  impl->vk.ttcks.dbi_dm.dbi.buffer,
                  impl->vk.ttcks.dbi_dm.dbi.offset,
                  sizeof(SPN_TYPE_U32VEC4) * 2,
                  0);

  //
  // This command buffer ends with a transfer
  //
  return VK_PIPELINE_STAGE_TRANSFER_BIT;
}

//
//
//
static void
spinel_ci_unsealed_reset(struct spinel_composition_impl * impl)
{
  //
  // otherwise... kick off a zeroing fill
  //
  impl->state = SPN_CI_STATE_RESETTING;

  //
  // acquire a signalling timeline
  //
  struct spinel_device * const device = impl->device;

  struct spinel_deps_immediate_submit_info disi = {
    .record = {
      .pfn   = spinel_ci_unsealed_reset_record,
      .data0 = impl,
    },
    .completion = {
      .pfn   = spinel_ci_unsealed_reset_complete,
      .data0 = impl,
    },
  };

  spinel_deps_immediate_submit(device->deps, &device->vk, &disi, &impl->signal.resetting.immediate);
}

//
//
//
static void
spinel_ci_block_until_sealed(struct spinel_composition_impl * impl)
{
  struct spinel_device * const device = impl->device;

  while (impl->state != SPN_CI_STATE_SEALED)
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }
}

//
//
//
static void
spinel_ci_block_while_resetting(struct spinel_composition_impl * impl)
{
  struct spinel_device * const device = impl->device;

  while (impl->state == SPN_CI_STATE_RESETTING)
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }
}

//
// wait for any in-flight renders to complete
//
static void
spinel_ci_sealed_unseal(struct spinel_composition_impl * impl)
{
  struct spinel_device * const device = impl->device;

  while (impl->lock_count > 0)
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  impl->state = SPN_CI_STATE_UNSEALED;
}

//
// FIXME(allanmac): add UNSEALING state
//
static spinel_result_t
spinel_ci_seal(struct spinel_composition_impl * impl)
{
  switch (impl->state)
    {
      case SPN_CI_STATE_RESETTING:
      case SPN_CI_STATE_UNSEALED:
        spinel_ci_unsealed_to_sealed(impl);
        return SPN_SUCCESS;

      case SPN_CI_STATE_SEALING:
        return SPN_SUCCESS;

      case SPN_CI_STATE_SEALED:
        // default:
        return SPN_SUCCESS;
    }
}

static spinel_result_t
spinel_ci_unseal(struct spinel_composition_impl * impl)
{
  switch (impl->state)
    {
      case SPN_CI_STATE_RESETTING:
      case SPN_CI_STATE_UNSEALED:
        return SPN_SUCCESS;

      case SPN_CI_STATE_SEALING:
        spinel_ci_block_until_sealed(impl);
        __attribute__((fallthrough));

      case SPN_CI_STATE_SEALED:
        // default:
        spinel_ci_sealed_unseal(impl);
        return SPN_SUCCESS;
    }
}

static spinel_result_t
spinel_ci_reset(struct spinel_composition_impl * impl)
{
  switch (impl->state)
    {
      case SPN_CI_STATE_RESETTING:
        return SPN_SUCCESS;

      case SPN_CI_STATE_UNSEALED:
        spinel_ci_unsealed_reset(impl);
        return SPN_SUCCESS;

      case SPN_CI_STATE_SEALING:
        return SPN_ERROR_COMPOSITION_SEALED;

      case SPN_CI_STATE_SEALED:
        // default:
        return SPN_ERROR_COMPOSITION_SEALED;
    }
}

//
//
//
static spinel_result_t
spinel_ci_set_clip(struct spinel_composition_impl * impl, spinel_pixel_clip_t const * clip)
{
  switch (impl->state)
    {
      case SPN_CI_STATE_RESETTING:
      case SPN_CI_STATE_UNSEALED:
        break;

      case SPN_CI_STATE_SEALING:
      case SPN_CI_STATE_SEALED:
      default:
        return SPN_ERROR_COMPOSITION_SEALED;
    }

  //
  // Set up the place clip
  //
  struct spinel_target_config const * const config = &impl->device->ti.config;

  uint32_t const tile_w = 1 << config->tile.width_log2;
  uint32_t const tile_h = 1 << config->tile.height_log2;

  uint32_t const surf_w = tile_w << SPN_TTCK_HI_BITS_X;
  uint32_t const surf_h = tile_h << SPN_TTCK_HI_BITS_Y;

  uint32_t const clip_x0 = MIN_MACRO(uint32_t, clip->x0, surf_w);
  uint32_t const clip_y0 = MIN_MACRO(uint32_t, clip->y0, surf_h);

  uint32_t const tile_w_mask = tile_w - 1;
  uint32_t const tile_h_mask = tile_h - 1;

  uint32_t const clip_x1 = MIN_MACRO(uint32_t, clip->x1, surf_w) + tile_w_mask;
  uint32_t const clip_y1 = MIN_MACRO(uint32_t, clip->y1, surf_h) + tile_h_mask;

  //
  // Note that impl->clip is an i32vec4
  //
  impl->clip.x = clip_x0 >> config->tile.width_log2;
  impl->clip.y = clip_y0 >> config->tile.height_log2;
  impl->clip.z = clip_x1 >> config->tile.width_log2;
  impl->clip.w = clip_y1 >> config->tile.height_log2;

  return SPN_SUCCESS;
}

//
//
//
static spinel_result_t
spinel_ci_place(struct spinel_composition_impl * impl,
                spinel_raster_t const *          rasters,
                spinel_layer_id const *          layer_ids,
                spinel_txty_t const *            txtys,
                uint32_t                         count)
{
  struct spinel_device * const device = impl->device;

  switch (impl->state)
    {
      case SPN_CI_STATE_RESETTING:
        spinel_ci_block_while_resetting(impl);
        break;

      case SPN_CI_STATE_UNSEALED:
        break;

      case SPN_CI_STATE_SEALING:
      case SPN_CI_STATE_SEALED:
      default:
        return SPN_ERROR_COMPOSITION_SEALED;
    }

  //
  // Nothing to do?
  //
  if (count == 0)
    {
      return SPN_SUCCESS;
    }

  //
  // Validate there is enough room for retained rasters
  //
  // Note that this is why we have to block if RESETTING.
  //
  if (impl->rasters.count + count > impl->rasters.size)
    {
      return SPN_ERROR_COMPOSITION_TOO_MANY_RASTERS;
    }

#ifndef NDEBUG
  //
  // NOTE(allanmac): No, we should never need to perform this test. The layer
  // invoking Spinel should ensure that layer ids remain below LAYER_MAX.
  //
  // Furthermore, the styling layer range is almost always far smaller than the
  // LAYER_MAX.
  //
  // Validate range of layer ids
  //
  for (uint32_t ii = 0; ii < count; ii++)
    {
      if (layer_ids[ii] > SPN_TTCK_LAYER_MAX)
        {
          return SPN_ERROR_LAYER_ID_INVALID;
        }
    }
#endif

  //
  // Validate first and then retain the rasters before we proceed
  //
  spinel_result_t result = spinel_device_validate_d_rasters(device, rasters, count);

  if (result != SPN_SUCCESS)
    {
      return result;
    }

  //
  // No survivable errors from here onward... any failure beyond here will be
  // fatal to the context!
  //
  spinel_device_retain_d_rasters(device, rasters, count);

  //
  // Save the rasters but update the dispatch head incrementally
  //
  spinel_handle_t * const rasters_base = impl->rasters.extent + impl->rasters.count;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      rasters_base[ii] = rasters[ii].handle;
    }

  //
  // copy place commands into the ring
  //
  struct spinel_ring * const ring = &impl->mapped.cp.ring;

  while (true)
    {
      //
      // how many slots left in ring?
      //
      uint32_t const head_nowrap = spinel_ring_head_nowrap(ring);
      uint32_t       avail       = MIN_MACRO(uint32_t, count, head_nowrap);

      //
      // if ring is full then this implies we're already waiting on
      // dispatches because an eager launch would've occurred
      //
      if (avail == 0)
        {
          spinel_deps_drain_1(device->deps, &device->vk);
          continue;
        }

      //
      // update rasters count incrementally
      //
      impl->rasters.count += avail;

      //
      // update dispatch
      //
      struct spinel_ci_dispatch * const dispatch = spinel_ci_dispatch_head(impl);

      dispatch->cp.span += avail;

      count -= avail;

      //
      // append commands to ring
      //
      struct spinel_cmd_place * cmds = impl->mapped.cp.extent + ring->head;

      spinel_ring_drop_n(ring, avail);

      if (txtys == NULL)
        {
          while (avail-- > 0)
            {
              cmds->raster_h = rasters->handle;
              cmds->layer_id = *layer_ids;
              cmds->txty[0]  = 0;
              cmds->txty[1]  = 0;

              ++rasters;
              ++layer_ids;
              ++cmds;
            }
        }
      else
        {
          while (avail-- > 0)
            {
              cmds->raster_h = rasters->handle;
              cmds->layer_id = *layer_ids;
              cmds->txty[0]  = txtys->tx;
              cmds->txty[1]  = txtys->ty;

              ++rasters;
              ++layer_ids;
              ++txtys;
              ++cmds;
            }
        }

      //
      // launch place kernel?
      //
      struct spinel_target_config const * const config = &device->ti.config;

      if (dispatch->cp.span >= config->composition.size.eager)
        {
          spinel_ci_place_flush(impl);
        }

      //
      // anything left?
      //
      if (count == 0)
        {
          return SPN_SUCCESS;
        }
    }
}

//
//
//

static spinel_result_t
spinel_ci_release(struct spinel_composition_impl * impl)
{
  //
  // wait for resetting to complete
  //
  struct spinel_device * const device = impl->device;

  spinel_ci_block_while_resetting(impl);

  //
  // wait for any in-flight PLACE dispatches to complete
  //
  while (!spinel_ring_is_full(&impl->dispatches.ring))
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  //
  // wait for any in-flight renders to complete
  //
  while (impl->lock_count > 0)
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  //
  // release any retained rasters
  //
  if (impl->rasters.count > 0)
    {
      spinel_device_release_d_rasters(impl->device, impl->rasters.extent, impl->rasters.count);
    }

  //
  // free Radix Sort extents
  //
  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.rs.indirect);

  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.rs.internal);

  //
  // free ttck_keyvals
  //
  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.ttck_keyvals_odd);

  //
  // free ttcks
  //
  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.ttcks.dbi_dm);
  //
  // free rings
  //
  if (spinel_ci_is_staged(&device->ti.config))
    {
      spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                                   device->vk.d,
                                   device->vk.ac,
                                   &impl->vk.rings.d.dbi_dm);
    }

  //
  // note that we don't have to unmap before freeing
  //
  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.hw_dr,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.rings.h.dbi_dm);
  //
  // free host allocations
  //
  free(impl->rasters.extent);
  free(impl->dispatches.extent);
  free(impl->composition);
  free(impl);

  spinel_context_release(device->context);

  return SPN_SUCCESS;
}

//
//
//

spinel_result_t
spinel_composition_impl_create(struct spinel_device *       device,
                               struct spinel_composition ** composition)
{
  spinel_context_retain(device->context);

  //
  // Allocate impl
  //
  struct spinel_composition_impl * const impl = malloc(sizeof(*impl));

  //
  // Allocate composition
  //
  struct spinel_composition * const c = *composition = malloc(sizeof(*c));

  //
  // Init back-pointers
  //
  impl->composition = c;
  c->impl           = impl;

  // Save device
  impl->device = device;

  // No locks
  impl->lock_count = 0;

  // Start in an unsealed state
  impl->state = SPN_CI_STATE_UNSEALED;

  //
  // initialize composition
  //
  c->release   = spinel_ci_release;
  c->place     = spinel_ci_place;
  c->seal      = spinel_ci_seal;
  c->unseal    = spinel_ci_unseal;
  c->reset     = spinel_ci_reset;
  c->set_clip  = spinel_ci_set_clip;
  c->ref_count = 1;

  //
  // Default to max clip
  //
  impl->clip = (SPN_TYPE_I32VEC4){ .x = 0,
                                   .y = 0,
                                   .z = 1 << SPN_TTCK_HI_BITS_X,
                                   .w = 1 << SPN_TTCK_HI_BITS_Y };

  //
  // Get config
  //
  struct spinel_target_config const * const config = &device->ti.config;

  //
  // Allocate and map ring
  //
  size_t const ring_size = config->composition.size.ring * sizeof(*impl->mapped.cp.extent);

  spinel_ring_init(&impl->mapped.cp.ring, config->composition.size.ring);

  spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.hw_dr,
                                        device->vk.pd,
                                        device->vk.d,
                                        device->vk.ac,
                                        ring_size,
                                        NULL,
                                        &impl->vk.rings.h);

  vk(MapMemory(device->vk.d,
               impl->vk.rings.h.dbi_dm.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.cp.extent));

  if (spinel_ci_is_staged(config))
    {
      spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.drw,
                                            device->vk.pd,
                                            device->vk.d,
                                            device->vk.ac,
                                            ring_size,
                                            NULL,
                                            &impl->vk.rings.d);
    }
  else
    {
      impl->vk.rings.d = impl->vk.rings.h;
    }

  //
  // Allocate ttcks extent
  //
  // clang-format off
  VkDeviceSize const ttck_keyvals_size = config->composition.size.ttcks * sizeof(SPN_TYPE_U32VEC2);
  VkDeviceSize const ttcks_size        = sizeof(SPN_BUFFER_TYPE(ttcks)) + ttck_keyvals_size;
  // clang-format on

  spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.drw,
                                        device->vk.pd,
                                        device->vk.d,
                                        device->vk.ac,
                                        ttcks_size,
                                        NULL,
                                        &impl->vk.ttcks);

  //
  // Allocate ttck_keyvals_odd extent
  //
  spinel_allocator_alloc_dbi_dm(&device->allocator.device.perm.drw,
                                device->vk.pd,
                                device->vk.d,
                                device->vk.ac,
                                ttck_keyvals_size,
                                NULL,
                                &impl->vk.ttck_keyvals_odd);

  //
  // Get radix sort memory requirements
  //
  struct radix_sort_vk_memory_requirements rs_mr;

  radix_sort_vk_get_memory_requirements(device->ti.rs, config->composition.size.ttcks, &rs_mr);

  assert(SPN_MEMBER_ALIGN_LIMIT >= rs_mr.keyvals_alignment);

  //
  // Allocate radix sort internal and indirect buffers
  //
  spinel_allocator_alloc_dbi_dm(&device->allocator.device.perm.drw,
                                device->vk.pd,
                                device->vk.d,
                                device->vk.ac,
                                rs_mr.internal_size,
                                NULL,
                                &impl->vk.rs.internal);

  spinel_allocator_alloc_dbi_dm(&device->allocator.device.perm.drw,
                                device->vk.pd,
                                device->vk.d,
                                device->vk.ac,
                                rs_mr.indirect_size,
                                NULL,
                                &impl->vk.rs.indirect);

  //
  // How many dispatches?
  //
  uint32_t const max_in_flight = config->composition.size.dispatches;

  //
  // Check worst case number of immediates is supported:
  //
  //  max_in_flight + resetting
  //
  assert(max_in_flight + 1 <= SPN_DEPS_IMMEDIATE_SUBMIT_SIZE_WAIT_IMMEDIATE);

  //
  // Allocate handle retention extent
  //
  size_t const d_size = sizeof(*impl->dispatches.extent) * max_in_flight;
  size_t const r_size = sizeof(*impl->rasters.extent) * config->composition.size.rasters;

  impl->dispatches.extent = malloc(d_size);
  impl->rasters.extent    = malloc(r_size);
  impl->rasters.size      = config->composition.size.rasters;
  impl->rasters.count     = 0;

  spinel_ring_init(&impl->dispatches.ring, max_in_flight);

  //
  // Initialize the first dispatch
  //
  spinel_ci_dispatch_init(impl, impl->dispatches.extent);

  //
  // Kick off resetting...
  //
  spinel_ci_unsealed_reset(impl);

  return SPN_SUCCESS;
}

//
//
//
spinel_deps_immediate_semaphore_t
spinel_composition_retain_and_lock(struct spinel_composition * composition)
{
  struct spinel_composition_impl * const impl = composition->impl;

  assert(impl->state >= SPN_CI_STATE_SEALING);

  spinel_composition_retain(composition);

  composition->impl->lock_count += 1;

  return impl->signal.sealing.immediate;
}

//
//
//
void
spinel_composition_unlock_and_release(struct spinel_composition * composition)
{
  composition->impl->lock_count -= 1;

  spinel_composition_release(composition);
}

//
//
//
void
spinel_composition_push_render_dispatch_record(struct spinel_composition * composition,
                                               VkCommandBuffer             cb)
{
  struct spinel_composition_impl * const impl   = composition->impl;
  struct spinel_device * const           device = impl->device;

  struct spinel_push_render_dispatch push_render_dispatch = {

    .devaddr_ttcks_header = impl->vk.ttcks.devaddr
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.render_dispatch,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_render_dispatch),
                     &push_render_dispatch);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.render_dispatch);

  vkCmdDispatch(cb, 1, 1, 1);
}

//
// 1. Initialize RENDER push constants with composition bufrefs
// 2. Record composition-driven indirect dispatch command
//
void
spinel_composition_push_render_init_record(struct spinel_composition * composition,
                                           struct spinel_push_render * push_render,
                                           VkCommandBuffer             cb)
{
  struct spinel_composition_impl * const impl   = composition->impl;
  struct spinel_device * const           device = impl->device;

  push_render->devaddr_ttcks_header = impl->vk.ttcks.devaddr;
  push_render->devaddr_ttck_keyvals = impl->vk.ttck_keyvals_out.devaddr;

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.render,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(*push_render),
                     push_render);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.render);

  VkDeviceSize const ttcks_offset_render_dispatch = SPN_BUFFER_OFFSETOF(ttcks, render_dispatch);

  vkCmdDispatchIndirect(cb, impl->vk.ttcks.dbi_dm.dbi.buffer, ttcks_offset_render_dispatch);
}

//
//
//
