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

#include "block_pool.h"
#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "core_vk.h"
#include "device.h"
#include "dispatch.h"
#include "queue_pool.h"
#include "ring.h"
#include "spinel_assert.h"
#include "vk.h"
#include "vk_target.h"

//
// The handle pool allocates host-side path and raster handles.
//
// The handles are reference counted and have both an external "host"
// reference count as well as an internal "device" reference count.
//
// The device reference count indicates that the handle is being
// processed by a sub-pipeline and ensures that the handle isn't
// reclaimed immediately after it is defined and before it's
// materialized.
//
// There are two large extents:
//
//   - a host-side pool of available handles
//   - a host-side array of handle reference counts
//
// The bulk size of the three extents is 8 bytes of overhead per
// number of host handles plus additional storage for storing blocks
// of handles.
//
// The number of host handles is usually less than the number of block
// ids in the block pool.
//
// Note that the maximum number of blocks is currently 2^27 -- the
// number of blocks is less than or equal to the sublocks count.
//
// A practical instantiation might provide a combined 2^20 path and
// raster host handles. This would occupy over 8 MB of host RAM for
// the 32-bit handles, the 32-bit reference counts and additional
// storage for the "blocks of handles".
//
// For 2^20 handles, the device would allocate 4 MB of memory to map
// handles to block pool ids.
//
// Notes:
//
//   - All host-side handles are stringently validated before being
//     retained or released.  If any handle is invalid, the entire set
//     of handles is rejected.
//
//   - The handle reference count is defensive and will not allow the
//     host to underflow a handle that's still retained by the
//     pipeline.
//
//   - The single reference counter is split into host and device
//     counts.
//

typedef uint32_t spn_handle_refcnt_hd;
typedef uint32_t spn_handle_refcnt_h;
typedef uint32_t spn_handle_refcnt_d;

//
// clang-format off
//

#define SPN_HANDLE_REFCNT_DEVICE_BITS  16
#define SPN_HANDLE_REFCNT_HOST_BITS    (32 - SPN_HANDLE_REFCNT_DEVICE_BITS)

#define SPN_HANDLE_REFCNT_DEVICE_MAX   BITS_TO_MASK_MACRO(SPN_HANDLE_REFCNT_DEVICE_BITS)
#define SPN_HANDLE_REFCNT_HOST_MAX     BITS_TO_MASK_MACRO(SPN_HANDLE_REFCNT_HOST_BITS)

//
// The reference count packs two counters in one 32-bit word
//
//  0              31
//  | HOST | DEVICE |
//  +------+--------+
//  |  16  |   16   |
//
//
// TODO(allanmac): The number of bits allocated to the device might
// become much lower.
//

union spn_handle_refcnt
{
  spn_handle_refcnt_hd   hd; // host and device

  struct
  {
    spn_handle_refcnt_h  h : SPN_HANDLE_REFCNT_HOST_BITS;
    spn_handle_refcnt_d  d : SPN_HANDLE_REFCNT_DEVICE_BITS;
  };
};

STATIC_ASSERT_MACRO_1(sizeof(union spn_handle_refcnt) == sizeof(spn_handle_refcnt_hd));

//
// make sure these sizes always match
//

STATIC_ASSERT_MACRO_1(sizeof(struct spn_path)   == sizeof(spn_handle_t));
STATIC_ASSERT_MACRO_1(sizeof(struct spn_raster) == sizeof(spn_handle_t));

//
// simple type punning
//

union spn_paths_to_handles
{
  struct spn_path const * const paths;
  spn_handle_t    const * const handles;
};

union spn_rasters_to_handles
{
  struct spn_raster const * const rasters;
  spn_handle_t      const * const handles;
};

//
//
//

struct spn_handle_pool_vk_dbi_dm
{
  VkDescriptorBufferInfo dbi;
  VkDeviceMemory         dm;
};

//
//
//

struct spn_handle_pool_handle_ring
{
  spn_handle_t *  extent;
  struct spn_ring ring;
};


//
//
//

struct spn_handle_pool_dispatch
{
  uint32_t head;
  uint32_t span;

  bool     complete;
};

//
//
//

struct spn_handle_pool_dispatch_ring
{
  struct spn_handle_pool_dispatch * extent;
  struct spn_ring                   ring;
};

//
//
//

typedef void (*spn_handle_pool_reclaim_pfn_flush)(struct spn_device * const device);

struct spn_handle_pool_reclaim
{
  spn_handle_pool_reclaim_pfn_flush    pfn_flush;

  struct spn_handle_pool_vk_dbi_dm     vk;
  struct spn_handle_pool_handle_ring   mapped;
  struct spn_handle_pool_dispatch_ring dispatches;
};

//
//
//

struct spn_handle_pool
{
  // ring of handles
  struct spn_handle_pool_handle_ring   handles;

  // array of reference counts indexed by a handle
  union spn_handle_refcnt *            refcnts;


  struct spn_handle_pool_reclaim       paths;
  struct spn_handle_pool_reclaim       rasters;
};

//
//
//

struct spn_handle_pool_reclaim_complete_payload
{
  struct spn_device *               device;
  struct spn_handle_pool_reclaim *  reclaim;
  struct spn_handle_pool_dispatch * dispatch;
  struct spn_vk_ds_reclaim_t        ds_reclaim;
};

//
// clang-format on
//

static struct spn_handle_pool_dispatch *
spn_handle_pool_reclaim_dispatch_head(struct spn_handle_pool_reclaim * const reclaim)
{
  return (reclaim->dispatches.extent + reclaim->dispatches.ring.head);
}

static struct spn_handle_pool_dispatch *
spn_handle_pool_reclaim_dispatch_tail(struct spn_handle_pool_reclaim * const reclaim)
{
  return (reclaim->dispatches.extent + reclaim->dispatches.ring.tail);
}

//
//
//

static void
spn_handle_pool_reclaim_dispatch_init(struct spn_handle_pool_reclaim * const reclaim)
{
  struct spn_handle_pool_dispatch * const wip = spn_handle_pool_reclaim_dispatch_head(reclaim);

  *wip = (struct spn_handle_pool_dispatch){ .head     = reclaim->mapped.ring.head,
                                            .span     = 0,
                                            .complete = false };
}

//
//
//

static void
spn_handle_pool_reclaim_dispatch_drop(struct spn_handle_pool_reclaim * const reclaim)
{
  struct spn_ring * const ring = &reclaim->dispatches.ring;

  spn_ring_drop_1(ring);
}

//
//
//

static void
spn_handle_pool_reclaim_dispatch_acquire(struct spn_handle_pool_reclaim * const reclaim,
                                         struct spn_device * const              device)
{
  struct spn_ring * const ring = &reclaim->dispatches.ring;

  while (spn_ring_is_empty(ring))
    {
      spn(device_wait(device, __func__));
    }

  spn_handle_pool_reclaim_dispatch_init(reclaim);
}

//
// See Vulkan specification's "Required Limits" section.
//

// clang-format off
#define SPN_VK_MAX_NONCOHERENT_ATOM_SIZE    256
#define SPN_VK_MAX_NONCOHERENT_ATOM_HANDLES (SPN_VK_MAX_NONCOHERENT_ATOM_SIZE / sizeof(spn_handle_t))
// clang-format on

//
//
//

static void
spn_handle_pool_reclaim_create(struct spn_handle_pool_reclaim * const reclaim,
                               struct spn_device * const              device,
                               uint32_t const                         count_handles,
                               uint32_t                               count_dispatches)
{
  //
  // allocate device ring
  //
  spn_ring_init(&reclaim->mapped.ring, count_handles);

  uint32_t const count_handles_ru = ROUND_UP_POW2_MACRO(count_handles,  //
                                                        SPN_VK_MAX_NONCOHERENT_ATOM_HANDLES);

  VkDeviceSize const size_ru = sizeof(*reclaim->mapped.extent) * count_handles_ru;

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.hrw_dr,
                                  &device->environment,
                                  size_ru,
                                  NULL,
                                  &reclaim->vk.dbi,
                                  &reclaim->vk.dm);

  //
  // map device ring
  //
  vk(MapMemory(device->environment.d,
               reclaim->vk.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&reclaim->mapped.extent));

  //
  // allocate and init dispatch ring
  //
  spn_ring_init(&reclaim->dispatches.ring, count_dispatches);

  size_t const size_dispatches = sizeof(*reclaim->dispatches.extent) * count_dispatches;

  reclaim->dispatches.extent = spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                                             SPN_MEM_FLAGS_READ_WRITE,
                                                             size_dispatches);

  //
  // init first dispatch
  //
  spn_handle_pool_reclaim_dispatch_init(reclaim);
}

//
//
//

static void
spn_handle_pool_reclaim_dispose(struct spn_handle_pool_reclaim * const reclaim,
                                struct spn_device * const              device)
{
  //
  // free host allocations
  //
  spn_allocator_host_perm_free(&device->allocator.host.perm, reclaim->dispatches.extent);

  //
  // free device allocations
  //
  spn_allocator_device_perm_free(&device->allocator.device.perm.hrw_dr,
                                 &device->environment,
                                 &reclaim->vk.dbi,
                                 reclaim->vk.dm);
}

//
//
//

static void
spn_handle_pool_copy(struct spn_ring * const    from_ring,
                     spn_handle_t const * const from,
                     struct spn_ring * const    to_ring,
                     spn_handle_t * const       to,
                     uint32_t                   span)
{
  while (span > 0)
    {
      uint32_t from_nowrap = spn_ring_tail_nowrap(from_ring);
      uint32_t to_nowrap   = spn_ring_tail_nowrap(to_ring);
      uint32_t min_nowrap  = MIN_MACRO(uint32_t, from_nowrap, to_nowrap);
      uint32_t span_nowrap = MIN_MACRO(uint32_t, min_nowrap, span);

      memcpy(to + to_ring->tail, from + from_ring->tail, sizeof(*to) * span_nowrap);

      spn_ring_release_n(from_ring, span_nowrap);
      spn_ring_release_n(to_ring, span_nowrap);

      span -= span_nowrap;
    }
}

//
//
//

static void
spn_handle_pool_reclaim_complete(void * const pfn_payload)
{
  struct spn_handle_pool_reclaim_complete_payload const * const payload = pfn_payload;

  // immediately release descriptor set
  spn_vk_ds_release_reclaim(payload->device->instance, payload->ds_reclaim);

  struct spn_handle_pool * const         handle_pool = payload->device->handle_pool;
  struct spn_handle_pool_reclaim * const reclaim     = payload->reclaim;
  struct spn_handle_pool_dispatch *      dispatch    = payload->dispatch;

  //
  // If the dispatch is the tail of the ring then release as many
  // completed dispatch records as possible.
  //
  // Note that kernels can complete in any order so the release records
  // need to be added to release ring slots in order.
  //
  if (reclaim->mapped.ring.tail == dispatch->head)
    {
      while (true)
        {
          // copy from mapped to handles
          spn_handle_pool_copy(&reclaim->mapped.ring,
                               reclaim->mapped.extent,
                               &handle_pool->handles.ring,
                               handle_pool->handles.extent,
                               dispatch->span);

          // release the dispatch
          spn_ring_release_n(&reclaim->dispatches.ring, 1);

          // any dispatches in flight?
          if (spn_ring_is_full(&reclaim->dispatches.ring))
            break;

          // get next dispatch
          dispatch = spn_handle_pool_reclaim_dispatch_tail(reclaim);

          // is this dispatch still in flight?
          if (!dispatch->complete)
            break;
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
spn_handle_pool_reclaim_flush_mapped(VkDevice       vk_d,  //
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
  mmr[0].offset = sizeof(spn_handle_t) * idx_rd;
  mmr[0].size   = sizeof(spn_handle_t) * (idx_hi_ru - idx_rd);

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
      mmr[1].size   = sizeof(spn_handle_t) * span_lo_ru;

      vk(FlushMappedMemoryRanges(vk_d, 2, mmr));
    }
}

//
// NOTE: the flush_paths() and flush_rasters() functions are nearly
// indentical but they might diverge in the future so there is no need
// to refactor.
//

static void
spn_device_handle_pool_flush_paths(struct spn_device * const device)
{
  struct spn_handle_pool * const          handle_pool = device->handle_pool;
  struct spn_handle_pool_reclaim * const  reclaim     = &handle_pool->paths;
  struct spn_handle_pool_dispatch * const wip = spn_handle_pool_reclaim_dispatch_head(reclaim);

  // anything to do?
  if (wip->span == 0)
    return;

  //
  // if ring is not coherent then flush
  //
  struct spn_vk * const                     instance = device->instance;
  struct spn_vk_target_config const * const config   = spn_vk_get_config(instance);

  if ((config->allocator.device.hrw_dr.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
      spn_handle_pool_reclaim_flush_mapped(device->environment.d,
                                           reclaim->vk.dm,
                                           reclaim->mapped.ring.size,
                                           wip->head,
                                           wip->span);
    }

  //
  // acquire an id for this stage
  //
  spn_dispatch_id_t id;

  spn(device_dispatch_acquire(device, SPN_DISPATCH_STAGE_RECLAIM_PATHS, &id));

  //
  // bind descriptor set, push constants and pipeline
  //
  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, id);

  // bind global BLOCK_POOL descriptor set
  spn_vk_ds_bind_paths_reclaim_block_pool(instance, cb, spn_device_block_pool_get_ds(device));

  // acquire RECLAIM descriptor set
  struct spn_vk_ds_reclaim_t ds_r;

  spn_vk_ds_acquire_reclaim(instance, device, &ds_r);

  // store the dbi
  *spn_vk_ds_get_reclaim_reclaim(instance, ds_r) = reclaim->vk.dbi;

  // update RECLAIM descriptor set
  spn_vk_ds_update_reclaim(instance, &device->environment, ds_r);

  // bind RECLAIM descriptor set
  spn_vk_ds_bind_paths_reclaim_reclaim(instance, cb, ds_r);

  // init and bind push constants
  struct spn_vk_push_paths_reclaim const push = {

    .bp_mask   = spn_device_block_pool_get_mask(device),
    .ring_size = reclaim->mapped.ring.size,
    .ring_head = wip->head,
    .ring_span = wip->span
  };

  spn_vk_p_push_paths_reclaim(instance, cb, &push);

  // bind the RECLAIM_PATHS pipeline
  spn_vk_p_bind_paths_reclaim(instance, cb);

  //
  // FIXME(allanmac): dispatch based on workgroup size
  //

  // dispatch one workgroup per reclamation block
  vkCmdDispatch(cb, wip->span, 1, 1);

  //
  // set completion routine
  //
  struct spn_handle_pool_reclaim_complete_payload * const payload =
    spn_device_dispatch_set_completion(device,
                                       id,
                                       spn_handle_pool_reclaim_complete,
                                       sizeof(*payload));

  payload->device     = device;
  payload->ds_reclaim = ds_r;
  payload->reclaim    = reclaim;
  payload->dispatch   = wip;

  //
  // the current dispatch is now "in flight" so drop it
  //
  spn_handle_pool_reclaim_dispatch_drop(reclaim);

  //
  // submit the dispatch
  //
  spn_device_dispatch_submit(device, id);

  //
  // acquire and initialize the next
  //
  spn_handle_pool_reclaim_dispatch_acquire(reclaim, device);
}

//
// NOTE: the flush_paths() and flush_rasters() functions are nearly
// indentical but they might diverge in the future so there is no need
// to refactor.
//

static void
spn_device_handle_pool_flush_rasters(struct spn_device * const device)
{
  struct spn_handle_pool * const          handle_pool = device->handle_pool;
  struct spn_handle_pool_reclaim * const  reclaim     = &handle_pool->rasters;
  struct spn_handle_pool_dispatch * const wip = spn_handle_pool_reclaim_dispatch_head(reclaim);

  // anything to do?
  if (wip->span == 0)
    return;

  //
  // if ring is not coherent then flush
  //
  struct spn_vk * const                     instance = device->instance;
  struct spn_vk_target_config const * const config   = spn_vk_get_config(instance);

  if ((config->allocator.device.hrw_dr.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
      spn_handle_pool_reclaim_flush_mapped(device->environment.d,
                                           reclaim->vk.dm,
                                           reclaim->mapped.ring.size,
                                           wip->head,
                                           wip->span);
    }

  //
  // acquire an id for this stage
  //
  spn_dispatch_id_t id;

  spn(device_dispatch_acquire(device, SPN_DISPATCH_STAGE_RECLAIM_RASTERS, &id));

  //
  // bind descriptor set, push constants and pipeline
  //
  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, id);

  // bind global BLOCK_POOL descriptor set
  spn_vk_ds_bind_rasters_reclaim_block_pool(instance, cb, spn_device_block_pool_get_ds(device));

  // acquire RECLAIM descriptor set
  struct spn_vk_ds_reclaim_t ds_r;

  spn_vk_ds_acquire_reclaim(instance, device, &ds_r);

  // store the dbi
  *spn_vk_ds_get_reclaim_reclaim(instance, ds_r) = reclaim->vk.dbi;

  // update RECLAIM descriptor set
  spn_vk_ds_update_reclaim(instance, &device->environment, ds_r);

  // bind RECLAIM descriptor set
  spn_vk_ds_bind_rasters_reclaim_reclaim(instance, cb, ds_r);

  // init and bind push constants
  struct spn_vk_push_rasters_reclaim const push = {

    .bp_mask   = spn_device_block_pool_get_mask(device),
    .ring_size = reclaim->mapped.ring.size,
    .ring_head = wip->head,
    .ring_span = wip->span
  };

  spn_vk_p_push_rasters_reclaim(instance, cb, &push);

  // bind the RECLAIM_RASTERS pipeline
  spn_vk_p_bind_rasters_reclaim(instance, cb);

  //
  // FIXME(allanmac): dispatch based on workgroup size
  //

  // dispatch one workgroup per reclamation block
  vkCmdDispatch(cb, wip->span, 1, 1);

  //
  // set completion routine
  //
  struct spn_handle_pool_reclaim_complete_payload * const payload =
    spn_device_dispatch_set_completion(device,
                                       id,
                                       spn_handle_pool_reclaim_complete,
                                       sizeof(*payload));

  payload->device     = device;
  payload->reclaim    = reclaim;
  payload->dispatch   = wip;
  payload->ds_reclaim = ds_r;

  //
  // the current dispatch is now sealed so drop it
  //
  spn_handle_pool_reclaim_dispatch_drop(reclaim);

  //
  // submit the dispatch
  //
  spn_device_dispatch_submit(device, id);

  //
  // acquire and initialize the next dispatch
  //
  spn_handle_pool_reclaim_dispatch_acquire(reclaim, device);
}

//
//
//

void
spn_device_handle_pool_create(struct spn_device * const device, uint32_t const handle_count)
{
  //
  // allocate the structure
  //
  struct spn_handle_pool * const handle_pool =
    spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  sizeof(*handle_pool));

  device->handle_pool = handle_pool;

  //
  //
  // allocate and init handles
  //
  spn_ring_init(&handle_pool->handles.ring, handle_count);

  size_t const size_handles = sizeof(*handle_pool->handles.extent) * handle_count;

  handle_pool->handles.extent = spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                                              SPN_MEM_FLAGS_READ_WRITE,
                                                              size_handles);
  for (uint32_t ii = 0; ii < handle_count; ii++)
    {
      handle_pool->handles.extent[ii] = ii;
    }

  //
  // allocate and init refcnts
  //
  size_t const size_refcnts = sizeof(*handle_pool->refcnts) * handle_count;

  handle_pool->refcnts = spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                                       SPN_MEM_FLAGS_READ_WRITE,
                                                       size_refcnts);
  memset(handle_pool->refcnts, 0, size_refcnts);

  //
  // initialize the reclamation rings
  //
  struct spn_vk_target_config const * const config = spn_vk_get_config(device->instance);

  spn_handle_pool_reclaim_create(&handle_pool->paths,
                                 device,
                                 config->reclaim.size.paths,
                                 config->reclaim.size.dispatches);

  spn_handle_pool_reclaim_create(&handle_pool->rasters,
                                 device,
                                 config->reclaim.size.rasters,
                                 config->reclaim.size.dispatches);

  //
  // initialize the flush pfns
  //
  handle_pool->paths.pfn_flush   = spn_device_handle_pool_flush_paths;
  handle_pool->rasters.pfn_flush = spn_device_handle_pool_flush_rasters;
}

//
//
//

void
spn_device_handle_pool_dispose(struct spn_device * const device)
{
  struct spn_handle_pool * const handle_pool = device->handle_pool;

  //
  // There is currently no need to drain the reclamation rings before
  // disposal because the entire context is being disposed.  Any
  // in-flight submissions will be drained elsewhere.
  //

  // free reclamation rings
  spn_handle_pool_reclaim_dispose(&handle_pool->rasters, device);
  spn_handle_pool_reclaim_dispose(&handle_pool->paths, device);

  // free host allocations
  spn_allocator_host_perm_free(&device->allocator.host.perm, handle_pool->refcnts);
  spn_allocator_host_perm_free(&device->allocator.host.perm, handle_pool->handles.extent);
  spn_allocator_host_perm_free(&device->allocator.host.perm, handle_pool);
}

//
//
//

uint32_t
spn_device_handle_pool_get_handle_count(struct spn_device * const device)
{
  return device->handle_pool->handles.ring.size;
}

//
//
//

static void
spn_device_handle_pool_reclaim_h(struct spn_device * const              device,
                                 struct spn_handle_pool_reclaim * const reclaim,
                                 union spn_handle_refcnt * const        refcnts,
                                 spn_handle_t const *                   handles,
                                 uint32_t                               count)
{
  struct spn_vk_target_config const * const config = spn_vk_get_config(device->instance);

  //
  // add handles to linear ring spans until done
  //
  while (count > 0)
    {
      //
      // how many ring slots are available?
      //
      uint32_t head_nowrap;

      while ((head_nowrap = spn_ring_head_nowrap(&reclaim->mapped.ring)) == 0)
        {
          // no need to flush here -- a flush would've already occurred
          spn(device_wait(device, __func__));
        }

      //
      // copy all releasable handles to the linear ring span
      //
      spn_handle_t * extent = reclaim->mapped.extent + reclaim->mapped.ring.head;
      uint32_t       rem    = head_nowrap;

      do
        {
          count -= 1;

          spn_handle_t const              handle     = *handles++;
          union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
          union spn_handle_refcnt         refcnt     = *refcnt_ptr;

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
      spn_ring_drop_n(&reclaim->mapped.ring, span);

      struct spn_handle_pool_dispatch * const wip = spn_handle_pool_reclaim_dispatch_head(reclaim);

      wip->span += span;

      //
      // flush?
      //
      if (wip->span >= config->reclaim.size.eager)
        {
          reclaim->pfn_flush(device);
        }
    }
}

//
//
//

static void
spn_device_handle_pool_reclaim_d(struct spn_device * const              device,
                                 struct spn_handle_pool_reclaim * const reclaim,
                                 union spn_handle_refcnt * const        refcnts,
                                 spn_handle_t const *                   handles,
                                 uint32_t                               count)
{
  struct spn_vk_target_config const * const config = spn_vk_get_config(device->instance);

  //
  // add handles to linear ring spans until done
  //
  while (count > 0)
    {
      //
      // how many ring slots are available?
      //
      uint32_t head_nowrap;

      while ((head_nowrap = spn_ring_head_nowrap(&reclaim->mapped.ring)) == 0)
        {
          // no need to flush here -- a flush would've already occurred
          spn(device_wait(device, __func__));
        }

      //
      // copy all releasable handles to the linear ring span
      //
      spn_handle_t * extent = reclaim->mapped.extent + reclaim->mapped.ring.head;
      uint32_t       rem    = head_nowrap;

      do
        {
          count -= 1;

          spn_handle_t const              handle     = *handles++;
          union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
          union spn_handle_refcnt         refcnt     = *refcnt_ptr;

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
      spn_ring_drop_n(&reclaim->mapped.ring, span);

      struct spn_handle_pool_dispatch * const wip = spn_handle_pool_reclaim_dispatch_head(reclaim);

      wip->span += span;

      //
      // flush?
      //
      if (wip->span >= config->reclaim.size.eager)
        {
          reclaim->pfn_flush(device);
        }
    }
}

//
// NOTE(allanmac): A batch-oriented version of this function will likely
// be required when the batch API is exposed.  For now, the Spinel API
// is implicitly acquiring one handle at a time.
//

void
spn_device_handle_pool_acquire(struct spn_device * const device, spn_handle_t * const p_handle)
{
  //
  // FIXME(allanmac): Running out of handles usually implies the app is
  // not reclaiming unused handles or the handle pool is too small.
  // Either case can be considered fatal unless reclamations are in
  // flight.
  //
  // This condition may need to be surfaced through the API ... or
  // simply kill the device with spn_device_lost() and log the reason.
  //
  // A comprehensive solution can be surfaced *after* the block pool
  // allocation becomes more precise.
  //
  struct spn_handle_pool * const handle_pool = device->handle_pool;
  struct spn_ring * const        ring        = &handle_pool->handles.ring;

  while (ring->rem == 0)
    {
      // flush both reclamation rings
      bool const flushable_paths   = !spn_ring_is_full(&handle_pool->paths.mapped.ring);
      bool const flushable_rasters = !spn_ring_is_full(&handle_pool->rasters.mapped.ring);

      if (!flushable_paths && !flushable_rasters)
        {
          spn_device_lost(device);
        }
      else
        {
          if (flushable_paths)
            {
              spn_device_handle_pool_flush_paths(device);
            }

          if (flushable_rasters)
            {
              spn_device_handle_pool_flush_rasters(device);
            }

          spn(device_wait(device, __func__));
        }
    }

  uint32_t const     idx    = spn_ring_acquire_1(ring);
  spn_handle_t const handle = handle_pool->handles.extent[idx];

  handle_pool->refcnts[handle] = (union spn_handle_refcnt){ .h = 1, .d = 1 };

  *p_handle = handle;
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

static spn_result_t
spn_device_handle_pool_validate_retain_h(struct spn_device * const  device,
                                         spn_handle_t const * const handles,
                                         uint32_t                   count)
{
  struct spn_handle_pool * const  handle_pool = device->handle_pool;
  union spn_handle_refcnt * const refcnts     = handle_pool->refcnts;
  uint32_t const                  handle_max  = handle_pool->handles.ring.size;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      if (handle >= handle_max)
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          union spn_handle_refcnt const refcnt = refcnts[handle];

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
      spn_handle_t const handle = handles[ii];

      refcnts[handle].h++;
    }

  return SPN_SUCCESS;
}

spn_result_t
spn_device_handle_pool_validate_retain_h_paths(struct spn_device * const     device,
                                               struct spn_path const * const paths,
                                               uint32_t                      count)
{
  union spn_paths_to_handles const p2h = { paths };

  return spn_device_handle_pool_validate_retain_h(device, p2h.handles, count);
}

spn_result_t
spn_device_handle_pool_validate_retain_h_rasters(struct spn_device * const       device,
                                                 struct spn_raster const * const rasters,
                                                 uint32_t                        count)
{
  union spn_rasters_to_handles const r2h = { rasters };

  return spn_device_handle_pool_validate_retain_h(device, r2h.handles, count);
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

static spn_result_t
spn_device_handle_pool_validate_release_h(struct spn_handle_pool * const  handle_pool,
                                          union spn_handle_refcnt * const refcnts,
                                          spn_handle_t const * const      handles,
                                          uint32_t                        count)
{
  uint32_t const handle_max = handle_pool->handles.ring.size;

  //
  // validate
  //
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      if (handle >= handle_max)
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          union spn_handle_refcnt const refcnt = refcnts[handle];

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
//
//

spn_result_t
spn_device_handle_pool_validate_release_h_paths(struct spn_device * const device,
                                                struct spn_path const *   paths,
                                                uint32_t                  count)
{
  struct spn_handle_pool * const   handle_pool = device->handle_pool;
  union spn_handle_refcnt * const  refcnts     = handle_pool->refcnts;
  union spn_paths_to_handles const p2h         = { paths };

  spn_result_t const result = spn_device_handle_pool_validate_release_h(handle_pool,  //
                                                                        refcnts,
                                                                        p2h.handles,
                                                                        count);

  if (result == SPN_SUCCESS)
    {
      spn_device_handle_pool_reclaim_h(device, &handle_pool->paths, refcnts, p2h.handles, count);
    }

  return result;
}

spn_result_t
spn_device_handle_pool_validate_release_h_rasters(struct spn_device * const device,
                                                  struct spn_raster const * rasters,
                                                  uint32_t                  count)
{
  struct spn_handle_pool * const     handle_pool = device->handle_pool;
  union spn_handle_refcnt * const    refcnts     = handle_pool->refcnts;
  union spn_rasters_to_handles const r2h         = { rasters };

  spn_result_t const result = spn_device_handle_pool_validate_release_h(handle_pool,  //
                                                                        refcnts,
                                                                        r2h.handles,
                                                                        count);

  if (result == SPN_SUCCESS)
    {
      spn_device_handle_pool_reclaim_h(device, &handle_pool->rasters, refcnts, r2h.handles, count);
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

static spn_result_t
spn_device_handle_pool_validate_retain_d(struct spn_device * const  device,
                                         spn_handle_t const * const handles,
                                         uint32_t const             count)
{
  struct spn_handle_pool * const  handle_pool = device->handle_pool;
  union spn_handle_refcnt * const refcnts     = handle_pool->refcnts;
  uint32_t const                  handle_max  = handle_pool->handles.ring.size;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      if (handle >= handle_max)
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          union spn_handle_refcnt const refcnt = refcnts[handle];

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

spn_result_t
spn_device_handle_pool_validate_d_paths(struct spn_device * const     device,
                                        struct spn_path const * const paths,
                                        uint32_t const                count)
{
  union spn_paths_to_handles const p2h = { paths };

  return spn_device_handle_pool_validate_retain_d(device, p2h.handles, count);
}

spn_result_t
spn_device_handle_pool_validate_d_rasters(struct spn_device * const       device,
                                          struct spn_raster const * const rasters,
                                          uint32_t const                  count)
{
  union spn_rasters_to_handles const r2h = { rasters };

  return spn_device_handle_pool_validate_retain_d(device, r2h.handles, count);
}

//
// After explicit validation, retain the handles for the device
//

static void
spn_device_handle_pool_retain_d(struct spn_device * const  device,
                                spn_handle_t const * const handles,
                                uint32_t const             count)

{
  struct spn_handle_pool * const  handle_pool = device->handle_pool;
  union spn_handle_refcnt * const refcnts     = handle_pool->refcnts;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      refcnts[handle].d++;
    }
}

void
spn_device_handle_pool_retain_d_paths(struct spn_device * const     device,
                                      struct spn_path const * const paths,
                                      uint32_t const                count)
{
  union spn_paths_to_handles const p2h = { paths };

  spn_device_handle_pool_retain_d(device, p2h.handles, count);
}

void
spn_device_handle_pool_retain_d_rasters(struct spn_device * const       device,
                                        struct spn_raster const * const rasters,
                                        uint32_t const                  count)
{
  union spn_rasters_to_handles const r2h = { rasters };

  spn_device_handle_pool_retain_d(device, r2h.handles, count);
}

//
// Release device-held spans of handles of known type
//

void
spn_device_handle_pool_release_d_paths(struct spn_device * const device,
                                       spn_handle_t const *      handles,
                                       uint32_t                  count)
{
  struct spn_handle_pool * const         handle_pool = device->handle_pool;
  struct spn_handle_pool_reclaim * const reclaim     = &handle_pool->paths;
  union spn_handle_refcnt * const        refcnts     = handle_pool->refcnts;

  spn_device_handle_pool_reclaim_d(device, reclaim, refcnts, handles, count);
}

void
spn_device_handle_pool_release_d_rasters(struct spn_device * const device,
                                         spn_handle_t const *      handles,
                                         uint32_t                  count)
{
  struct spn_handle_pool * const         handle_pool = device->handle_pool;
  struct spn_handle_pool_reclaim * const reclaim     = &handle_pool->rasters;
  union spn_handle_refcnt * const        refcnts     = handle_pool->refcnts;

  spn_device_handle_pool_reclaim_d(device, reclaim, refcnts, handles, count);
}

//
// Release handles on a ring -- up to two spans
//

void
spn_device_handle_pool_release_ring_d_paths(struct spn_device * const device,
                                            spn_handle_t const *      paths,
                                            uint32_t const            size,
                                            uint32_t const            head,
                                            uint32_t const            span)
{
  struct spn_handle_pool * const         handle_pool = device->handle_pool;
  struct spn_handle_pool_reclaim * const reclaim     = &handle_pool->paths;
  union spn_handle_refcnt * const        refcnts     = handle_pool->refcnts;

  uint32_t const head_max = head + span;
  uint32_t       count_lo = MIN_MACRO(uint32_t, head_max, size) - head;

  spn_device_handle_pool_reclaim_d(device, reclaim, refcnts, paths + head, count_lo);

  if (span > count_lo)
    {
      uint32_t count_hi = span - count_lo;

      spn_device_handle_pool_reclaim_d(device, reclaim, refcnts, paths, count_hi);
    }
}

void
spn_device_handle_pool_release_ring_d_rasters(struct spn_device * const device,
                                              spn_handle_t const *      rasters,
                                              uint32_t const            size,
                                              uint32_t const            head,
                                              uint32_t const            span)
{
  struct spn_handle_pool * const         handle_pool = device->handle_pool;
  struct spn_handle_pool_reclaim * const reclaim     = &handle_pool->rasters;
  union spn_handle_refcnt * const        refcnts     = handle_pool->refcnts;

  uint32_t const head_max = head + span;
  uint32_t       count_lo = MIN_MACRO(uint32_t, head_max, size) - head;

  spn_device_handle_pool_reclaim_d(device, reclaim, refcnts, rasters + head, count_lo);

  if (span > count_lo)
    {
      uint32_t count_hi = span - count_lo;

      spn_device_handle_pool_reclaim_d(device, reclaim, refcnts, rasters, count_hi);
    }
}

//
//
//
