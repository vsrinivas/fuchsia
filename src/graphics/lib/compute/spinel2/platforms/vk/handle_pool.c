// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "handle_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "ring.h"
#include "shaders/push.h"
#include "spinel/spinel_assert.h"

//
// The handle pool allocates host-side path and raster handles.
//
// The handles are reference counted and have both an external "host" reference
// count as well as an internal "device" reference count.
//
// The device reference count indicates that the handle is being processed by a
// sub-pipeline and ensures that the handle isn't reclaimed immediately after it
// is defined and before it's materialized.
//
// There are several large host extents:
//
//   - a host-side pool of available handles         --   4 bytes
//   - a host-side array of handle reference counts  --   4 bytes
//   - a host-side array of handle semaphore indices -- 1-2 bytes
//
// And one large device extent:
//
//   - a device-side handle-to-block id map -- 4 bytes
//
// The aggregate size of the three host extents is ~9-10 bytes of overhead per
// number of host handles plus additional storage for storing blocks of handles.
//
// The device extent allocates ~4 bytes per handle.
//
// The number of host handles should be less than the number of block ids in the
// block pool.
//
// Note that the maximum number of blocks is currently 2^27 -- the number of
// blocks is less than or equal to the sublocks count.
//
// A practical instantiation might provide a combined 2^20 path and raster host
// handles. This would occupy 9-10 MB of host RAM for the 32-bit handles, the
// 32-bit reference counts and additional storage for the "blocks of handles".
//
// Notes:
//
//   - All host-side handles are stringently validated before being retained or
//     released.  If any handle is invalid, the entire set of handles is
//     rejected.
//
//   - The handle reference count is defensive and will not allow the host to
//     underflow a handle that's still retained by the pipeline.
//
//   - The single reference counter is split into host and device counts.
//
//   - There is currently a reference count limit of USHORT_MAX on both host and
//     device.  If this is deemed to be too small, then increase the reference
//     count union to 64 bits.
//
typedef uint32_t spinel_handle_refcnt_hd;
typedef uint32_t spinel_handle_refcnt_h;
typedef uint32_t spinel_handle_refcnt_d;

//
//
// clang-format off
#define SPN_HANDLE_REFCNT_DEVICE_BITS  16
#define SPN_HANDLE_REFCNT_HOST_BITS    (32 - SPN_HANDLE_REFCNT_DEVICE_BITS)

#define SPN_HANDLE_REFCNT_DEVICE_MAX   BITS_TO_MASK_MACRO(SPN_HANDLE_REFCNT_DEVICE_BITS)
#define SPN_HANDLE_REFCNT_HOST_MAX     BITS_TO_MASK_MACRO(SPN_HANDLE_REFCNT_HOST_BITS)

//
// The reference count packs two counters in one 32-bit word:
//
//   0              31
//   | HOST | DEVICE |
//   +------+--------+
//   |  16  |   16   |
//
union spinel_handle_refcnt
{
  spinel_handle_refcnt_hd   hd; // host and device

  struct
  {
    spinel_handle_refcnt_h  h : SPN_HANDLE_REFCNT_HOST_BITS;
    spinel_handle_refcnt_d  d : SPN_HANDLE_REFCNT_DEVICE_BITS;
  };
};


//
// Doublecheck some size assumptions in case modifications are made.
//
STATIC_ASSERT_MACRO_1(sizeof(union spinel_handle_refcnt) == sizeof(spinel_handle_refcnt_hd));
STATIC_ASSERT_MACRO_1(sizeof(struct spinel_path)   == sizeof(spinel_handle_t));
STATIC_ASSERT_MACRO_1(sizeof(struct spinel_raster) == sizeof(spinel_handle_t));

//
// Type punning unions
//
union spinel_paths_to_handles
{
  struct spinel_path const * const paths;
  spinel_handle_t    const * const handles;
};

union spinel_rasters_to_handles
{
  struct spinel_raster const * const rasters;
  spinel_handle_t      const * const handles;
};
// clang-format on

//
// See Vulkan specification's "Required Limits" section.
//
// clang-format off
#define SPN_VK_MAX_NONCOHERENT_ATOM_SIZE    256
#define SPN_VK_MAX_NONCOHERENT_ATOM_HANDLES (SPN_VK_MAX_NONCOHERENT_ATOM_SIZE / sizeof(spinel_handle_t))
// clang-format on

//
// Handle ring allocator
//
struct spinel_handle_pool_handle_ring
{
  spinel_handle_t *  extent;
  struct spinel_ring ring;
};

//
// Dispatches can complete in any order but are reclaimed in ring order.
//
struct spinel_handle_pool_dispatch
{
  struct
  {
    uint32_t head;
    uint32_t span;
  } ring;

  bool complete;
};

//
// Vulkan dispatch pool
//
struct spinel_handle_pool_dispatch_ring
{
  struct spinel_handle_pool_dispatch * extent;
  struct spinel_ring                   ring;
};

//
//
//
typedef void (*spinel_handle_pool_reclaim_flush_pfn)(struct spinel_device * device);

//
//
//
struct spinel_handle_pool_reclaim
{
  struct spinel_dbi_dm_devaddr            vk;
  struct spinel_handle_pool_handle_ring   mapped;
  struct spinel_handle_pool_dispatch_ring dispatches;
};

//
//
//
struct spinel_handle_pool
{
  //
  // The handles, refcnts, and semaphore indices
  //
  struct spinel_handle_pool_handle_ring handles;
  union spinel_handle_refcnt *          refcnts;
  spinel_deps_delayed_semaphore_t *     semaphores;

  //
  // Separate reclamation accounting for paths and rasters
  //
  struct spinel_handle_pool_reclaim paths;
  struct spinel_handle_pool_reclaim rasters;
};

//
//
//
static struct spinel_handle_pool_dispatch *
spinel_handle_pool_reclaim_dispatch_head(struct spinel_handle_pool_reclaim * reclaim)
{
  return (reclaim->dispatches.extent + reclaim->dispatches.ring.head);
}

static struct spinel_handle_pool_dispatch *
spinel_handle_pool_reclaim_dispatch_tail(struct spinel_handle_pool_reclaim * reclaim)
{
  return (reclaim->dispatches.extent + reclaim->dispatches.ring.tail);
}

//
//
//
static void
spinel_handle_pool_reclaim_dispatch_init(struct spinel_handle_pool_reclaim * reclaim)
{
  // clang-format off
  struct spinel_handle_pool_dispatch * const wip = spinel_handle_pool_reclaim_dispatch_head(reclaim);
  // clang-format on

  //
  // Don't acquire a semaphore until necessary!
  //
  *wip = (struct spinel_handle_pool_dispatch){

    .ring.head = reclaim->mapped.ring.head,
    .ring.span = 0,
    .complete  = false,
  };
}

//
//
//
static void
spinel_handle_pool_reclaim_dispatch_drop(struct spinel_handle_pool_reclaim * reclaim)
{
  struct spinel_ring * const ring = &reclaim->dispatches.ring;

  spinel_ring_drop_1(ring);
}

//
//
//
static void
spinel_handle_pool_reclaim_dispatch_acquire(struct spinel_handle_pool_reclaim * reclaim,
                                            struct spinel_device *              device)
{
  //
  // Is there a dispatch available?
  //
  // If not, drain the deps ring until the head dispatch is marked complete.
  //
  struct spinel_ring * const ring = &reclaim->dispatches.ring;

  while (spinel_ring_is_empty(ring) && (reclaim->dispatches.extent[ring->head].complete == false))
    {
      spinel_deps_drain_1(device->deps, &device->vk, UINT64_MAX);
    }

  //
  // Initialize the head dispatch
  //
  spinel_handle_pool_reclaim_dispatch_init(reclaim);
}

//
//
//
static void
spinel_handle_pool_reclaim_create(struct spinel_handle_pool_reclaim * reclaim,
                                  struct spinel_device *              device,
                                  uint32_t const                      count_handles,
                                  uint32_t                            count_dispatches)
{
  //
  // allocate device ring
  //
  spinel_ring_init(&reclaim->mapped.ring, count_handles);

  // clang-format off
  uint32_t const count_handles_ru = ROUND_UP_POW2_MACRO(count_handles, SPN_VK_MAX_NONCOHERENT_ATOM_HANDLES);
  // clang-format on

  VkDeviceSize const extent_size = sizeof(*reclaim->mapped.extent) * count_handles_ru;

  spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.hrw_dr,
                                        device->vk.pd,
                                        device->vk.d,
                                        device->vk.ac,
                                        extent_size,
                                        NULL,
                                        &reclaim->vk);

  //
  // map device ring
  //
  vk(MapMemory(device->vk.d,  //
               reclaim->vk.dbi_dm.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&reclaim->mapped.extent));

  //
  // allocate and init dispatch ring
  //
  spinel_ring_init(&reclaim->dispatches.ring, count_dispatches);

  size_t const size_dispatches = sizeof(*reclaim->dispatches.extent) * count_dispatches;

  reclaim->dispatches.extent = malloc(size_dispatches);

  //
  // init first dispatch
  //
  spinel_handle_pool_reclaim_dispatch_init(reclaim);
}

//
//
//
static void
spinel_handle_pool_reclaim_dispose(struct spinel_handle_pool_reclaim * reclaim,
                                   struct spinel_device *              device)
{
  //
  // free host allocations
  //
  free(reclaim->dispatches.extent);

  //
  // free device allocations
  //
  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.hrw_dr,
                               device->vk.d,
                               device->vk.ac,
                               &reclaim->vk.dbi_dm);
}

//
//
//
static void
spinel_handle_pool_copy(struct spinel_ring *    from_ring,
                        spinel_handle_t const * from,
                        struct spinel_ring *    to_ring,
                        spinel_handle_t *       to,
                        uint32_t                span)
{
  while (span > 0)
    {
      uint32_t from_nowrap = spinel_ring_tail_nowrap(from_ring);
      uint32_t to_nowrap   = spinel_ring_tail_nowrap(to_ring);
      uint32_t min_nowrap  = MIN_MACRO(uint32_t, from_nowrap, to_nowrap);
      uint32_t span_nowrap = MIN_MACRO(uint32_t, min_nowrap, span);

      memcpy(to + to_ring->tail, from + from_ring->tail, sizeof(*to) * span_nowrap);

      spinel_ring_release_n(from_ring, span_nowrap);
      spinel_ring_release_n(to_ring, span_nowrap);

      span -= span_nowrap;
    }
}

//
//
//
static void
spinel_handle_pool_reclaim_flush_paths_complete(void * data0, void * data1)
{
  struct spinel_device *               device      = data0;
  struct spinel_handle_pool_dispatch * dispatch    = data1;
  struct spinel_handle_pool *          handle_pool = device->handle_pool;
  struct spinel_handle_pool_reclaim *  reclaim     = &handle_pool->paths;

  //
  // If the dispatch is the tail of the ring then release as many completed
  // dispatch records as possible.
  //
  // Note that kernels can complete in any order so the release records need to
  // be added to release ring slots in order.
  //
  if (reclaim->mapped.ring.tail == dispatch->ring.head)
    {
      while (true)
        {
          // copy from mapped to handles
          spinel_handle_pool_copy(&reclaim->mapped.ring,
                                  reclaim->mapped.extent,
                                  &handle_pool->handles.ring,
                                  handle_pool->handles.extent,
                                  dispatch->ring.span);

          // release the dispatch
          spinel_ring_release_n(&reclaim->dispatches.ring, 1);

          // any dispatches in flight?
          if (spinel_ring_is_full(&reclaim->dispatches.ring))
            {
              break;
            }

          // get next dispatch
          dispatch = spinel_handle_pool_reclaim_dispatch_tail(reclaim);

          // is this dispatch still in flight?
          if (!dispatch->complete)
            {
              break;
            }
        }
    }
  else
    {
      // out-of-order completion
      dispatch->complete = true;
    }
}

//
//
//
static void
spinel_handle_pool_reclaim_flush_rasters_complete(void * data0, void * data1)
{
  struct spinel_device *               device      = data0;
  struct spinel_handle_pool_dispatch * dispatch    = data1;
  struct spinel_handle_pool *          handle_pool = device->handle_pool;
  struct spinel_handle_pool_reclaim *  reclaim     = &handle_pool->rasters;

  //
  // If the dispatch is the tail of the ring then release as many completed
  // dispatch records as possible.
  //
  // Note that kernels can complete in any order so the release records need to
  // be added to release ring slots in order.
  //
  if (reclaim->mapped.ring.tail == dispatch->ring.head)
    {
      while (true)
        {
          // copy from mapped to handles
          spinel_handle_pool_copy(&reclaim->mapped.ring,
                                  reclaim->mapped.extent,
                                  &handle_pool->handles.ring,
                                  handle_pool->handles.extent,
                                  dispatch->ring.span);

          // release the dispatch
          spinel_ring_release_n(&reclaim->dispatches.ring, 1);

          // any dispatches in flight?
          if (spinel_ring_is_full(&reclaim->dispatches.ring))
            {
              break;
            }

          // get next dispatch
          dispatch = spinel_handle_pool_reclaim_dispatch_tail(reclaim);

          // is this dispatch still in flight?
          if (!dispatch->complete)
            {
              break;
            }
        }
    }
  else
    {
      // out-of-order completion
      dispatch->complete = true;
    }
}

//
// Flush the noncoherent mapped ring
//
static void
spinel_handle_pool_reclaim_flush_mapped(VkDevice       vk_d,  //
                                        VkDeviceMemory ring,
                                        uint32_t const size,
                                        uint32_t const head,
                                        uint32_t const span)
{
  uint32_t const idx_max = head + span;
  uint32_t const idx_hi  = MIN_MACRO(uint32_t, idx_max, size);
  uint32_t const span_hi = idx_hi - head;

  uint32_t const idx_rd    = ROUND_DOWN_POW2_MACRO(head, SPN_VK_MAX_NONCOHERENT_ATOM_HANDLES);
  uint32_t const idx_hi_ru = ROUND_UP_POW2_MACRO(idx_hi, SPN_VK_MAX_NONCOHERENT_ATOM_HANDLES);

  VkMappedMemoryRange mmr[2];

  mmr[0].sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  mmr[0].pNext  = NULL;
  mmr[0].memory = ring;
  mmr[0].offset = sizeof(spinel_handle_t) * idx_rd;
  mmr[0].size   = sizeof(spinel_handle_t) * (idx_hi_ru - idx_rd);

  if (span <= span_hi)
    {
      vk(FlushMappedMemoryRanges(vk_d, 1, mmr));
    }
  else
    {
      uint32_t const span_lo    = span - span_hi;
      uint32_t const span_lo_ru = ROUND_UP_POW2_MACRO(span_lo, SPN_VK_MAX_NONCOHERENT_ATOM_HANDLES);

      mmr[1].sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      mmr[1].pNext  = NULL;
      mmr[1].memory = ring;
      mmr[1].offset = 0;
      mmr[1].size   = sizeof(spinel_handle_t) * span_lo_ru;

      vk(FlushMappedMemoryRanges(vk_d, 2, mmr));
    }
}

//
// Record path reclamation commands
//
static VkPipelineStageFlags
spinel_handle_pool_reclaim_flush_paths_record(VkCommandBuffer cb, void * data0, void * data1)
{
  // clang-format off
  struct spinel_device * const                 device      = data0;
  struct spinel_handle_pool * const            handle_pool = device->handle_pool;
  struct spinel_handle_pool_reclaim * const    reclaim     = &handle_pool->paths;
  struct spinel_handle_pool_dispatch * const   wip         = spinel_handle_pool_reclaim_dispatch_head(reclaim);
  // clang-format on

  //
  // If ring is not coherent then flush
  //
  struct spinel_target_config const * const config = &device->ti.config;

  if ((config->allocator.device.hrw_dr.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
      spinel_handle_pool_reclaim_flush_mapped(device->vk.d,
                                              reclaim->vk.dbi_dm.dm,
                                              reclaim->mapped.ring.size,
                                              wip->ring.head,
                                              wip->ring.span);
    }

  //
  // Record commands
  //
  struct spinel_block_pool * const block_pool = &device->block_pool;

  struct spinel_push_reclaim const push = {
    .devaddr_reclaim             = handle_pool->paths.vk.devaddr,
    .devaddr_block_pool_blocks   = block_pool->vk.dbi_devaddr.blocks.devaddr,
    .devaddr_block_pool_ids      = block_pool->vk.dbi_devaddr.ids.devaddr,
    .devaddr_block_pool_host_map = block_pool->vk.dbi_devaddr.host_map.devaddr,
    .ring_size                   = reclaim->mapped.ring.size,
    .ring_head                   = wip->ring.head,
    .ring_span                   = wip->ring.span,
    .bp_mask                     = block_pool->bp_mask,
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.paths_reclaim,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push),
                     &push);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.paths_reclaim);

  //
  // dispatch a subgroup per span element
  //
  uint32_t const sgs_per_wg = config->group_sizes.named.paths_reclaim.workgroup >>
                              config->group_sizes.named.paths_reclaim.subgroup_log2;

  uint32_t const span_wgs = (wip->ring.span + sgs_per_wg - 1) / sgs_per_wg;

  vkCmdDispatch(cb, span_wgs, 1, 1);

  //
  // This command buffer ends with a compute shader
  //
  return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
}

//
// NOTE: the flush_paths() and flush_rasters() functions are nearly identical
// but they might diverge in the future so there is no need to refactor.
//
static void
spinel_handle_pool_reclaim_flush_paths(struct spinel_device * device)
{
  struct spinel_handle_pool * const         handle_pool = device->handle_pool;
  struct spinel_handle_pool_reclaim * const reclaim     = &handle_pool->paths;

  //
  // Acquire an immediate semaphore
  //
  struct spinel_deps_immediate_submit_info const disi = {
    .record     = {
      .pfn   = spinel_handle_pool_reclaim_flush_paths_record,
      .data0 = device,
    },
    .completion = {
      .pfn   = spinel_handle_pool_reclaim_flush_paths_complete,
      .data0 = device,
    },
  };

  //
  // NOTE: We don't need to save the immediate semaphore handle because context
  // creation will block and drain all submissions before returning.
  //
  (void)spinel_deps_immediate_submit(device->deps, &device->vk, &disi);

  //
  // The current dispatch is now "in flight" so drop it
  //
  spinel_handle_pool_reclaim_dispatch_drop(reclaim);

  //
  // Acquire and initialize the next dispatch
  //
  spinel_handle_pool_reclaim_dispatch_acquire(reclaim, device);
}

//
// Record raster reclamation commands
//
static VkPipelineStageFlags
spinel_handle_pool_reclaim_flush_rasters_record(VkCommandBuffer cb, void * data0, void * data1)
{
  // clang-format off
  struct spinel_device * const                 device      = data0;
  struct spinel_handle_pool * const            handle_pool = device->handle_pool;
  struct spinel_handle_pool_reclaim * const    reclaim     = &handle_pool->rasters;
  struct spinel_handle_pool_dispatch * const   wip         = spinel_handle_pool_reclaim_dispatch_head(reclaim);
  // clang-format on

  //
  // if ring is not coherent then flush
  //
  struct spinel_target_config const * const config = &device->ti.config;

  if ((config->allocator.device.hrw_dr.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
      spinel_handle_pool_reclaim_flush_mapped(device->vk.d,
                                              reclaim->vk.dbi_dm.dm,
                                              reclaim->mapped.ring.size,
                                              wip->ring.head,
                                              wip->ring.span);
    }

  //
  // Record commands
  //
  struct spinel_block_pool const * block_pool = &device->block_pool;

  struct spinel_push_reclaim const push = {
    .devaddr_reclaim             = handle_pool->rasters.vk.devaddr,
    .devaddr_block_pool_blocks   = block_pool->vk.dbi_devaddr.blocks.devaddr,
    .devaddr_block_pool_ids      = block_pool->vk.dbi_devaddr.ids.devaddr,
    .devaddr_block_pool_host_map = block_pool->vk.dbi_devaddr.host_map.devaddr,
    .ring_size                   = reclaim->mapped.ring.size,
    .ring_head                   = wip->ring.head,
    .ring_span                   = wip->ring.span,
    .bp_mask                     = block_pool->bp_mask,
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.rasters_reclaim,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push),
                     &push);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.rasters_reclaim);

  //
  // dispatch a subgroup per span element
  //
  uint32_t const sgs_per_wg = config->group_sizes.named.rasters_reclaim.workgroup >>
                              config->group_sizes.named.rasters_reclaim.subgroup_log2;

  uint32_t const span_wgs = (wip->ring.span + sgs_per_wg - 1) / sgs_per_wg;

  vkCmdDispatch(cb, span_wgs, 1, 1);

  //
  // This command buffer ends with a compute shader
  //
  return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
}

//
// NOTE: the flush_paths() and flush_rasters() functions are nearly identical
// but they might diverge in the future so there is no need to refactor.
//
static void
spinel_handle_pool_reclaim_flush_rasters(struct spinel_device * device)
{
  struct spinel_handle_pool * const         handle_pool = device->handle_pool;
  struct spinel_handle_pool_reclaim * const reclaim     = &handle_pool->rasters;

  //
  // Acquire an immediate semaphore
  //
  struct spinel_deps_immediate_submit_info const disi = {
    .record = {
      .pfn   = spinel_handle_pool_reclaim_flush_rasters_record,
      .data0 = device,
    },
    .completion = {
      .pfn   = spinel_handle_pool_reclaim_flush_rasters_complete,
      .data0 = device,
    },
  };

  //
  // NOTE: We don't need to save the immediate semaphore handle because context
  // creation will block and drain all submissions before returning.
  //
  (void)spinel_deps_immediate_submit(device->deps, &device->vk, &disi);

  //
  // The current dispatch is now "in flight" so drop it
  //
  spinel_handle_pool_reclaim_dispatch_drop(reclaim);

  //
  // Acquire and initialize the next dispatch
  //
  spinel_handle_pool_reclaim_dispatch_acquire(reclaim, device);
}

//
//
//
void
spinel_device_handle_pool_create(struct spinel_device * device, uint32_t handle_count)
{
  //
  //
  //
  struct spinel_handle_pool * const handle_pool = malloc(sizeof(*handle_pool));

  device->handle_pool = handle_pool;

  //
  //
  // allocate and init handles
  //
  spinel_ring_init(&handle_pool->handles.ring, handle_count);

  size_t const size_handles = sizeof(*handle_pool->handles.extent) * handle_count;

  handle_pool->handles.extent = malloc(size_handles);

  for (uint32_t ii = 0; ii < handle_count; ii++)
    {
      handle_pool->handles.extent[ii] = ii;
    }

  //
  // allocate and init refcnts
  //
  handle_pool->refcnts = calloc(handle_count, sizeof(*handle_pool->refcnts));

  //
  // initialize the reclamation rings
  //
  struct spinel_target_config const * const config = &device->ti.config;

  spinel_handle_pool_reclaim_create(&handle_pool->paths,
                                    device,
                                    config->reclaim.size.paths,
                                    config->reclaim.size.dispatches);

  spinel_handle_pool_reclaim_create(&handle_pool->rasters,
                                    device,
                                    config->reclaim.size.rasters,
                                    config->reclaim.size.dispatches);
}

//
// All in-flight submissions will have been drained.
//
void
spinel_device_handle_pool_dispose(struct spinel_device * device)
{
  struct spinel_handle_pool * const handle_pool = device->handle_pool;

  // free reclamation rings
  spinel_handle_pool_reclaim_dispose(&handle_pool->rasters, device);
  spinel_handle_pool_reclaim_dispose(&handle_pool->paths, device);

  // free host allocations
  free(handle_pool->refcnts);
  free(handle_pool->handles.extent);

  // free handle pool
  free(handle_pool);
}

//
//
//
uint32_t
spinel_handle_pool_get_handle_count(struct spinel_handle_pool const * handle_pool)
{
  return handle_pool->handles.ring.size;
}

//
//
//
static void
spinel_handle_pool_reclaim_h(struct spinel_handle_pool_reclaim *  reclaim,
                             spinel_handle_pool_reclaim_flush_pfn flush,
                             struct spinel_device *               device,
                             union spinel_handle_refcnt *         refcnts,
                             spinel_handle_t const *              handles,
                             uint32_t                             count)
{
  struct spinel_target_config const * const config = &device->ti.config;

  //
  // add handles to linear ring spans until done
  //
  while (count > 0)
    {
      //
      // how many ring slots are available?
      //
      uint32_t head_nowrap;

      while ((head_nowrap = spinel_ring_head_nowrap(&reclaim->mapped.ring)) == 0)
        {
          // no need to flush here -- a flush would've already occurred
          spinel_deps_drain_1(device->deps, &device->vk, UINT64_MAX);
        }

      //
      // copy all releasable handles to the linear ring span
      //
      spinel_handle_t * extent = reclaim->mapped.extent + reclaim->mapped.ring.head;
      uint32_t          rem    = head_nowrap;

      do
        {
          count -= 1;

          spinel_handle_t const              handle     = *handles++;
          union spinel_handle_refcnt * const refcnt_ptr = refcnts + handle;
          union spinel_handle_refcnt         refcnt     = *refcnt_ptr;

          refcnt.h--;

          *refcnt_ptr = refcnt;

          if (refcnt.hd == 0)
            {
              *extent++ = handle;

              if (--rem == 0)
                break;
            }
      } while (count > 0);

      //
      // were no handles appended?
      //
      uint32_t const span = head_nowrap - rem;

      if (span == 0)
        return;

      //
      // update ring
      //
      spinel_ring_drop_n(&reclaim->mapped.ring, span);

      // clang-format off
      struct spinel_handle_pool_dispatch * const wip = spinel_handle_pool_reclaim_dispatch_head(reclaim);
      // clang-format on

      wip->ring.span += span;

      //
      // eager flush?
      //
      if (wip->ring.span >= config->reclaim.size.eager)
        {
          flush(device);
        }
    }
}

//
//
//
static void
spinel_handle_pool_reclaim_d(struct spinel_handle_pool_reclaim *  reclaim,
                             spinel_handle_pool_reclaim_flush_pfn flush,
                             struct spinel_device *               device,
                             spinel_handle_t const *              handles,
                             uint32_t                             count)
{
  struct spinel_handle_pool * const         handle_pool = device->handle_pool;
  union spinel_handle_refcnt * const        refcnts     = handle_pool->refcnts;
  struct spinel_target_config const * const config      = &device->ti.config;

  //
  // add handles to linear ring spans until done
  //
  while (count > 0)
    {
      //
      // how many ring slots are available?
      //
      uint32_t head_nowrap;

      while ((head_nowrap = spinel_ring_head_nowrap(&reclaim->mapped.ring)) == 0)
        {
          // no need to flush here -- a flush would've already occurred
          spinel_deps_drain_1(device->deps, &device->vk, UINT64_MAX);
        }

      //
      // copy all releasable handles to the linear ring span
      //
      spinel_handle_t * extent = reclaim->mapped.extent + reclaim->mapped.ring.head;
      uint32_t          rem    = head_nowrap;

      do
        {
          count -= 1;

          spinel_handle_t const              handle     = *handles++;
          union spinel_handle_refcnt * const refcnt_ptr = refcnts + handle;
          union spinel_handle_refcnt         refcnt     = *refcnt_ptr;

          refcnt.d--;

          *refcnt_ptr = refcnt;

          if (refcnt.hd == 0)
            {
              *extent++ = handle;

              if (--rem == 0)
                break;
            }
      } while (count > 0);

      //
      // were no handles appended?
      //
      uint32_t const span = head_nowrap - rem;

      if (span == 0)
        return;

      //
      // update ring
      //
      spinel_ring_drop_n(&reclaim->mapped.ring, span);

      // clang-format off
      struct spinel_handle_pool_dispatch * const wip = spinel_handle_pool_reclaim_dispatch_head(reclaim);
      // clang-format on

      wip->ring.span += span;

      //
      // eager flush?
      //
      if (wip->ring.span >= config->reclaim.size.eager)
        {
          flush(device);
        }
    }
}

//
// NOTE(allanmac): A batch-oriented version of this function will likely
// be required when the batch API is exposed.  For now, the Spinel API
// is implicitly acquiring one handle at a time.
//
spinel_handle_t
spinel_device_handle_acquire(struct spinel_device * device)
{
  //
  // FIXME(allanmac): Running out of handles usually implies the app is not
  // reclaiming unused handles or the handle pool is too small.  Either case can
  // be considered fatal unless reclamations are in flight.
  //
  // This condition may need to be surfaced through the API ... or simply kill
  // the device with spinel_device_lost() and log the reason.
  //
  // A comprehensive solution can be surfaced *after* the block pool allocation
  // becomes more precise.
  //
  struct spinel_handle_pool * const handle_pool = device->handle_pool;
  struct spinel_ring * const        ring        = &handle_pool->handles.ring;

  while (ring->rem == 0)
    {
      //
      // flush both reclamation rings
      //
      bool const flushable_paths   = !spinel_ring_is_full(&handle_pool->paths.mapped.ring);
      bool const flushable_rasters = !spinel_ring_is_full(&handle_pool->rasters.mapped.ring);

      if (!flushable_paths && !flushable_rasters)
        {
          spinel_device_lost(device);
        }
      else
        {
          if (flushable_paths)
            {
              spinel_handle_pool_reclaim_flush_paths(device);
            }

          if (flushable_rasters)
            {
              spinel_handle_pool_reclaim_flush_rasters(device);
            }

          spinel_deps_drain_1(device->deps, &device->vk, UINT64_MAX);
        }
    }

  uint32_t const idx = spinel_ring_acquire_1(ring);

  spinel_handle_t handle = handle_pool->handles.extent[idx];

  handle_pool->refcnts[handle] = (union spinel_handle_refcnt){ .h = 1, .d = 1 };

  return handle;
}

//
// Validate host-provided handles before retaining.
//
// Retain validation consists of:
//
//   - correct handle type
//   - handle is in range of pool
//   - host refcnt is not zero
//   - host refcnt is not at the maximum value
//
// After validation, go ahead and retain the handles for the host.
//
static spinel_result_t
spinel_device_validate_retain_h(struct spinel_device *  device,
                                spinel_handle_t const * handles,
                                uint32_t                count)
{
  struct spinel_handle_pool * const  handle_pool = device->handle_pool;
  union spinel_handle_refcnt * const refcnts     = handle_pool->refcnts;
  uint32_t const                     handle_max  = handle_pool->handles.ring.size;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spinel_handle_t const handle = handles[ii];

      if (handle >= handle_max)
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          union spinel_handle_refcnt const refcnt = refcnts[handle];

          if (refcnt.h == 0)
            {
              return SPN_ERROR_HANDLE_INVALID;
            }
          else if (refcnt.h == SPN_HANDLE_REFCNT_HOST_MAX)
            {
              return SPN_ERROR_HANDLE_OVERFLOW;
            }
        }
    }

  //
  // all the handles validated, so retain them all..
  //
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spinel_handle_t const handle = handles[ii];

      refcnts[handle].h++;
    }

  return SPN_SUCCESS;
}

//
// Path and raster-specific functions
//
spinel_result_t
spinel_device_validate_retain_h_paths(struct spinel_device *     device,
                                      struct spinel_path const * paths,
                                      uint32_t                   count)
{
  union spinel_paths_to_handles const p2h = { paths };

  return spinel_device_validate_retain_h(device, p2h.handles, count);
}

spinel_result_t
spinel_device_validate_retain_h_rasters(struct spinel_device *       device,
                                        struct spinel_raster const * rasters,
                                        uint32_t                     count)
{
  union spinel_rasters_to_handles const r2h = { rasters };

  return spinel_device_validate_retain_h(device, r2h.handles, count);
}

//
// Validate host-provided handles before releasing.
//
// Release validation consists of:
//
//   - handle is in range of pool
//   - host refcnt is not zero
//
// After validation, release the handles for the host
//
static spinel_result_t
spinel_handle_pool_validate_release_h(struct spinel_handle_pool *  handle_pool,
                                      union spinel_handle_refcnt * refcnts,
                                      spinel_handle_t const *      handles,
                                      uint32_t                     count)
{
  uint32_t const handle_max = handle_pool->handles.ring.size;

  //
  // validate
  //
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spinel_handle_t const handle = handles[ii];

      if (handle >= handle_max)
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          union spinel_handle_refcnt const refcnt = refcnts[handle];

          if (refcnt.h == 0)
            {
              return SPN_ERROR_HANDLE_INVALID;
            }
        }
    }

  //
  // all the handles validated
  //
  return SPN_SUCCESS;
}

//
// Path and raster-specific functions
//
spinel_result_t
spinel_device_validate_release_h_paths(struct spinel_device *     device,
                                       struct spinel_path const * paths,
                                       uint32_t                   count)
{
  struct spinel_handle_pool * const   handle_pool = device->handle_pool;
  union spinel_handle_refcnt * const  refcnts     = handle_pool->refcnts;
  union spinel_paths_to_handles const p2h         = { paths };

  spinel_result_t const result = spinel_handle_pool_validate_release_h(handle_pool,  //
                                                                       refcnts,
                                                                       p2h.handles,
                                                                       count);

  if (result == SPN_SUCCESS)
    {
      spinel_handle_pool_reclaim_h(&handle_pool->paths,
                                   spinel_handle_pool_reclaim_flush_rasters,
                                   device,
                                   refcnts,
                                   p2h.handles,
                                   count);
    }

  return result;
}

spinel_result_t
spinel_device_validate_release_h_rasters(struct spinel_device *       device,
                                         struct spinel_raster const * rasters,
                                         uint32_t                     count)
{
  struct spinel_handle_pool * const     handle_pool = device->handle_pool;
  union spinel_handle_refcnt * const    refcnts     = handle_pool->refcnts;
  union spinel_rasters_to_handles const r2h         = { rasters };

  spinel_result_t const result = spinel_handle_pool_validate_release_h(handle_pool,  //
                                                                       refcnts,
                                                                       r2h.handles,
                                                                       count);

  if (result == SPN_SUCCESS)
    {
      spinel_handle_pool_reclaim_h(&handle_pool->rasters,
                                   spinel_handle_pool_reclaim_flush_rasters,
                                   device,
                                   refcnts,
                                   r2h.handles,
                                   count);
    }

  return result;
}

//
// Validate host-provided handles before retaining on the device.
//
//   - handle is in range of pool
//   - host refcnt is not zero
//   - device refcnt is not at the maximum value
//
static spinel_result_t
spinel_device_validate_retain_d(struct spinel_device *  device,
                                spinel_handle_t const * handles,
                                uint32_t const          count)
{
  struct spinel_handle_pool * const  handle_pool = device->handle_pool;
  union spinel_handle_refcnt * const refcnts     = handle_pool->refcnts;
  uint32_t const                     handle_max  = handle_pool->handles.ring.size;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spinel_handle_t const handle = handles[ii];

      if (handle >= handle_max)
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          union spinel_handle_refcnt const refcnt = refcnts[handle];

          if (refcnt.h == 0)
            {
              return SPN_ERROR_HANDLE_INVALID;
            }
          else if (refcnt.d == SPN_HANDLE_REFCNT_DEVICE_MAX)
            {
              return SPN_ERROR_HANDLE_OVERFLOW;
            }
        }
    }

  return SPN_SUCCESS;
}

spinel_result_t
spinel_device_validate_d_paths(struct spinel_device *     device,
                               struct spinel_path const * paths,
                               uint32_t const             count)
{
  union spinel_paths_to_handles const p2h = { paths };

  return spinel_device_validate_retain_d(device, p2h.handles, count);
}

spinel_result_t
spinel_device_validate_d_rasters(struct spinel_device *       device,
                                 struct spinel_raster const * rasters,
                                 uint32_t const               count)
{
  union spinel_rasters_to_handles const r2h = { rasters };

  return spinel_device_validate_retain_d(device, r2h.handles, count);
}

//
// After explicit validation, retain the handles for the device
//
static void
spinel_device_retain_d(struct spinel_device *  device,
                       spinel_handle_t const * handles,
                       uint32_t const          count)

{
  struct spinel_handle_pool * const  handle_pool = device->handle_pool;
  union spinel_handle_refcnt * const refcnts     = handle_pool->refcnts;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spinel_handle_t const handle = handles[ii];

      refcnts[handle].d++;
    }
}

void
spinel_device_retain_d_paths(struct spinel_device *     device,
                             struct spinel_path const * paths,
                             uint32_t const             count)
{
  union spinel_paths_to_handles const p2h = { paths };

  spinel_device_retain_d(device, p2h.handles, count);
}

void
spinel_device_retain_d_rasters(struct spinel_device *       device,
                               struct spinel_raster const * rasters,
                               uint32_t const               count)
{
  union spinel_rasters_to_handles const r2h = { rasters };

  spinel_device_retain_d(device, r2h.handles, count);
}

//
// Release device-held spans of handles of known type
//
void
spinel_device_release_d_paths(struct spinel_device *  device,
                              spinel_handle_t const * handles,
                              uint32_t                count)
{
  spinel_handle_pool_reclaim_d(&device->handle_pool->paths,
                               spinel_handle_pool_reclaim_flush_paths,
                               device,
                               handles,
                               count);
}

void
spinel_device_release_d_rasters(struct spinel_device *  device,
                                spinel_handle_t const * handles,
                                uint32_t                count)
{
  spinel_handle_pool_reclaim_d(&device->handle_pool->rasters,
                               spinel_handle_pool_reclaim_flush_rasters,
                               device,
                               handles,
                               count);
}

//
// Release handles on a ring -- up to two spans
//
void
spinel_device_release_d_paths_ring(struct spinel_device *  device,
                                   spinel_handle_t const * paths,
                                   uint32_t const          size,
                                   uint32_t const          head,
                                   uint32_t const          span)
{
  uint32_t const head_max   = head + span;
  uint32_t const head_clamp = MIN_MACRO(uint32_t, head_max, size);
  uint32_t const count_lo   = head_clamp - head;

  spinel_device_release_d_paths(device, paths + head, count_lo);

  if (span > count_lo)
    {
      uint32_t const count_hi = span - count_lo;

      spinel_device_release_d_paths(device, paths, count_hi);
    }
}

void
spinel_device_release_d_rasters_ring(struct spinel_device *  device,
                                     spinel_handle_t const * rasters,
                                     uint32_t const          size,
                                     uint32_t const          head,
                                     uint32_t const          span)
{
  uint32_t const head_max   = head + span;
  uint32_t const head_clamp = MIN_MACRO(uint32_t, head_max, size);
  uint32_t const count_lo   = head_clamp - head;

  spinel_device_release_d_rasters(device, rasters + head, count_lo);

  if (span > count_lo)
    {
      uint32_t const count_hi = span - count_lo;

      spinel_device_release_d_rasters(device, rasters, count_hi);
    }
}

//
//
//
