// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "handle_pool.h"

#include <string.h>

#include "block_pool.h"
#include "common/macros.h"
#include "common/vk/vk_assert.h"
#include "core_vk.h"
#include "device.h"
#include "extent.h"
#include "fence_pool.h"
#include "queue_pool.h"
#include "spn_vk.h"
#include "spn_vk_target.h"

//
// FIXME -- THIS DOCUMENTATION IS STALE NOW THAT A REFERENCE COUNT REP
// IS A {HOST:DEVICE} PAIR.
//
// Host-side handle pool
//
// The bulk size of the three extents is currently 6 bytes of overhead
// per number of host handles.  The number of host handles is usually
// less than the number of blocks in the pool.  Note that the maximum
// number of blocks is 2^27.
//
// A practical instantiation might provide a combined 2^20 path and
// raster host handles. This would occupy 6 MB of host RAM for the
// 32-bit handle, 8-bit reference count and 8-bit handle-to-grid map.
//
// Also note that we could use isolated/separate path and raster block
// pools. Worst case, this would double the memory footprint of SPN.
//
// Host-side handle reference count
//
//   [0      ] : release
//   [1..UMAX] : retain
//
// In a garbage-collected environment we might want to rely on an
// existing mechanism for determing whether a handle is live.
//
// Otherwise, we probably want to have a 16 or 32-bit ref count.
//
// The handle reference count is defensive and will not allow the host
// to underflow a handle that's still retained by the pipeline.
//
// The single reference counter is split into host and device counts.
//
//
// HANDLE/ACQUIRE RELEASE
//
// The device vends handles just in case we decide to exploit shared
// virtual memory.  But for most devices and devices we will have a
// pool of host-managed handles and on the device there will be a
// table that maps the host handle to a device-managed memory block.
//
// HANDLE READINESS
//
// A host handle may reference a path or a raster which is not ready
// for use further down the pipeline because it hasn't yet been
// processed by the device.
//
// The simplest scheme for providing every handle a readiness state is
// to build a map that that marks a new handle as being not-ready
// while being processed by a particular grid id.  When the final
// sub-pipeline grid responsible for the path or raster is complete,
// then mark the handle as being ready and eventually return the grid
// id back to the pool.  This can be performed on a separate thread.
//
// The side-benefit of this approach is that a handle's reference
// count integral type can spare some bits for its associated grid id.
//
// A more memory-intensive approach uses a 64-bit epoch+grid key and
// relies on the ~56 bits of epoch space to avoid any post
// sub-pipeline status update by assuming that a handle and grid will
// match or mismatch when queried.
//

typedef uint8_t  spn_handle_refcnt_h;
typedef uint8_t  spn_handle_refcnt_d;
typedef uint16_t spn_handle_refcnt_hd;

STATIC_ASSERT_MACRO_1(sizeof(spn_handle_refcnt_hd) ==
                      sizeof(spn_handle_refcnt_h) + sizeof(spn_handle_refcnt_d));

//
//
//

union spn_handle_refcnt
{
  spn_handle_refcnt_hd hd;  // host and device

  struct
  {
    spn_handle_refcnt_h h;  // host
    spn_handle_refcnt_d d;  // device
  };
};

//
//
//

typedef enum spn_handle_pool_reclaim_type_e
{
  SPN_HANDLE_POOL_RECLAIM_TYPE_PATH,
  SPN_HANDLE_POOL_RECLAIM_TYPE_RASTER,
  SPN_HANDLE_POOL_RECLAIM_TYPE_COUNT
} spn_handle_pool_reclaim_type_e;

struct spn_handle_pool_br
{
  uint32_t block;
  uint32_t rem;
};

//
//
//

struct spn_handle_pool
{
  struct spn_extent_pdrw map;  // device-managed extent mapping a host handle to device block id

  struct
  {
    spn_handle_t *            extent;   // array of individual host handles -- segmented into blocks
    union spn_handle_refcnt * refcnts;  // array of reference counts indexed by an individual handle
    uint32_t                  count;    // total number of handles
  } handle;

  struct
  {
    uint32_t   size;     // number of handles in a block
    uint32_t * indices;  // block indices
    struct
    {
      uint32_t avail;  // blocks with handles
      uint32_t empty;  // blocks with no handles
    } rem;
    uint32_t count;  // total number of indices
  } block;

  struct
  {
    struct spn_handle_pool_br acquire;
    struct spn_handle_pool_br reclaim[SPN_HANDLE_POOL_RECLAIM_TYPE_COUNT];  // FIXME -- need to pad
  } wip;
};

//
//
//

#define SPN_HANDLE_REFCNT_HOST_BITS (MEMBER_SIZE_MACRO(union spn_handle_refcnt, h) * 8)
#define SPN_HANDLE_REFCNT_DEVICE_BITS (MEMBER_SIZE_MACRO(union spn_handle_refcnt, d) * 8)

#define SPN_HANDLE_REFCNT_HOST_MAX BITS_TO_MASK_MACRO(SPN_HANDLE_REFCNT_HOST_BITS)
#define SPN_HANDLE_REFCNT_DEVICE_MAX BITS_TO_MASK_MACRO(SPN_HANDLE_REFCNT_DEVICE_BITS)

//
// Globally assume that the push constant limit is 256 bytes.
//
// This isn't the ideal place for this test but it will definitely
// halt device creation in a debug build.
//

#define SPN_HANDLE_POOL_MAX_PUSH_CONSTANTS_SIZE 256
#define SPN_HANDLE_POOL_MAX_RECLAIM_SIZE                                                           \
  ((SPN_HANDLE_POOL_MAX_PUSH_CONSTANTS_SIZE / sizeof(spn_handle_t)) - 1)

static uint32_t
spn_device_handle_pool_reclaim_size(struct spn_vk_target_config const * const config)
{
  uint32_t const paths = config->p.push_sizes.named.paths_reclaim;

#ifndef NDEBUG
  // make sure these remain the same
  uint32_t const rasters = config->p.push_sizes.named.rasters_reclaim;

  // double-check these two sizes match
  assert(paths == rasters);
#endif

  // reclaim size matches the push constant size
  return (paths - OFFSET_OF_MACRO(struct spn_vk_push_paths_reclaim, path_ids)) /
         sizeof(spn_handle_t);
}

//
//
//

void
spn_device_handle_pool_create(struct spn_device * const device, uint32_t const handle_count)
{
  struct spn_handle_pool * const handle_pool =
    spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  sizeof(*handle_pool));

  device->handle_pool = handle_pool;

  uint32_t const reclaim_size =
    spn_device_handle_pool_reclaim_size(spn_vk_get_config(device->target));

  uint32_t const blocks = (handle_count + reclaim_size - 1) / reclaim_size;
  uint32_t const blocks_padded =
    blocks + MAX_MACRO(uint32_t, /*block_pad*/ 0, SPN_HANDLE_POOL_RECLAIM_TYPE_COUNT);
  uint32_t const handles        = blocks * reclaim_size;
  uint32_t const handles_padded = blocks_padded * reclaim_size;

  spn_extent_pdrw_alloc(&handle_pool->map,
                        &device->allocator.device.perm.local,
                        device->environment,
                        handles * sizeof(spn_block_id_t));
  //
  // allocate handles
  //
  handle_pool->handle.extent =
    spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  handles_padded * sizeof(*handle_pool->handle.extent));

  handle_pool->handle.refcnts =
    spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  handles_padded * sizeof(*handle_pool->handle.refcnts));

  // initialize handles and refcnts
  for (uint32_t ii = 0; ii < handles; ii++)
    {
      handle_pool->handle.extent[ii]     = ii;
      handle_pool->handle.refcnts[ii].hd = 0;
    }

  handle_pool->handle.count = handles;

  //
  // allocate blocks of handles
  //
  handle_pool->block.indices =
    spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  blocks_padded * sizeof(*handle_pool->block.indices));

  // initialize block accounting
  for (uint32_t ii = 0; ii < blocks_padded; ii++)
    handle_pool->block.indices[ii] = ii;

  handle_pool->block.size      = reclaim_size;  // reclaim size for both paths and rasters
  handle_pool->block.rem.avail = blocks;
  handle_pool->block.rem.empty = blocks_padded - blocks;
  handle_pool->block.count     = blocks_padded;

  handle_pool->wip.acquire.rem = 0;

  // initialize reclaim/acquire
  for (uint32_t ii = 0; ii < SPN_HANDLE_POOL_RECLAIM_TYPE_COUNT; ii++)
    handle_pool->wip.reclaim[ii].rem = 0;
}

//
//
//

void
spn_device_handle_pool_dispose(struct spn_device * const device)
{
  struct spn_allocator_host_perm * const perm        = &device->allocator.host.perm;
  struct spn_handle_pool * const         handle_pool = device->handle_pool;

  spn_allocator_host_perm_free(perm, handle_pool->block.indices);
  spn_allocator_host_perm_free(perm, handle_pool->handle.refcnts);
  spn_allocator_host_perm_free(perm, handle_pool->handle.extent);

  spn_extent_pdrw_free(&handle_pool->map,
                       &device->allocator.device.perm.local,
                       device->environment);

  spn_allocator_host_perm_free(perm, device->handle_pool);
}

//
//
//

static uint32_t
spn_device_handle_pool_block_acquire_pop(struct spn_device * const      device,
                                         struct spn_handle_pool * const handle_pool)
{
  uint32_t avail;

  while ((avail = handle_pool->block.rem.avail) == 0)
    {
      spn_device_wait(device);
    }

  uint32_t idx = avail - 1;

  handle_pool->block.rem.avail = idx;

  return handle_pool->block.indices[idx];
}

static uint32_t
spn_device_handle_pool_block_reclaim_pop(struct spn_device * const      device,
                                         struct spn_handle_pool * const handle_pool)
{
  uint32_t empty;

  while ((empty = handle_pool->block.rem.empty) == 0)
    {
      spn_device_wait(device);
    }

  uint32_t idx = handle_pool->block.count - empty;

  handle_pool->block.rem.empty = empty - 1;

  return handle_pool->block.indices[idx];
}

//
//
//

static void
spn_device_handle_pool_block_acquire_push(struct spn_handle_pool * const handle_pool,
                                          uint32_t const                 block)
{
  uint32_t const idx              = handle_pool->block.rem.avail++;
  handle_pool->block.indices[idx] = block;
}

static void
spn_device_handle_pool_block_reclaim_push(struct spn_handle_pool * const handle_pool,
                                          uint32_t const                 block)
{
  uint32_t const idx              = handle_pool->block.count - ++handle_pool->block.rem.empty;
  handle_pool->block.indices[idx] = block;
}

//
//
//

struct spn_handle_pool_reclaim_complete_payload
{
  struct spn_handle_pool * handle_pool;
  uint32_t                 block;
};

STATIC_ASSERT_MACRO_1(sizeof(struct spn_handle_pool_reclaim_complete_payload) <=
                      SPN_FENCE_COMPLETE_PFN_PAYLOAD_SIZE_MAX);

static void
spn_handle_pool_reclaim_complete(void * const pfn_payload)
{
  //
  // FENCE_POOL INVARIANT:
  //
  // COMPLETION ROUTINE MUST MAKE LOCAL COPIES OF PAYLOAD BEFORE ANY
  // POTENTIAL INVOCATION OF SPN_DEVICE_YIELD/WAIT/DRAIN()
  //
  struct spn_handle_pool_reclaim_complete_payload const * const payload     = pfn_payload;
  struct spn_handle_pool * const                                handle_pool = payload->handle_pool;

  spn_device_handle_pool_block_acquire_push(handle_pool, payload->block);
}

//
// Launch reclamation grid:
//
// - acquire a command buffer
// - acquire reclamation descriptor set -- always zero for the block pool!
// - bind the block pool
// - initialize push constants
// - append the push constants
// - bind the pipeline
//

static void
spn_device_bind_paths_reclaim(struct spn_device * const device,
                              spn_handle_t * const      handles,
                              VkCommandBuffer           cb)
{
  struct spn_vk * const               instance = device->target;
  struct spn_vk_ds_block_pool_t const ds       = spn_device_block_pool_get_ds(device);

  spn_vk_ds_bind_paths_reclaim_block_pool(instance, cb, ds);

  union
  {
    uint8_t                          bytes[SPN_HANDLE_POOL_MAX_PUSH_CONSTANTS_SIZE];
    struct spn_vk_push_paths_reclaim reclaim;
  } push;

  push.reclaim.bp_mask = spn_device_block_pool_get_mask(device);

  //
  // FIXME -- any way to avoid this copy?  Only if the push constant
  // structure mirrored te reclamation structure so probabaly not.
  //
  memcpy(
    push.reclaim.path_ids,
    handles,
    spn_vk_get_config(instance)->p.push_sizes.named.paths_reclaim - sizeof(push.reclaim.bp_mask));

  spn_vk_p_push_paths_reclaim(instance, cb, &push.reclaim);
  spn_vk_p_bind_paths_reclaim(instance, cb);
}

static void
spn_device_bind_rasters_reclaim(struct spn_device * const device,
                                spn_handle_t * const      handles,
                                VkCommandBuffer           cb)
{
  struct spn_vk * const               instance = device->target;
  struct spn_vk_ds_block_pool_t const ds       = spn_device_block_pool_get_ds(device);

  spn_vk_ds_bind_rasters_reclaim_block_pool(instance, cb, ds);

  union
  {
    uint8_t                            bytes[SPN_HANDLE_POOL_MAX_PUSH_CONSTANTS_SIZE];
    struct spn_vk_push_rasters_reclaim reclaim;
  } push;

  push.reclaim.bp_mask = spn_device_block_pool_get_mask(device);

  memcpy(
    push.reclaim.raster_ids,
    handles,
    spn_vk_get_config(instance)->p.push_sizes.named.rasters_reclaim - sizeof(push.reclaim.bp_mask));

  spn_vk_p_push_rasters_reclaim(instance, cb, &push.reclaim);
  spn_vk_p_bind_rasters_reclaim(instance, cb);
}

//
//
//

static void
spn_device_handle_pool_reclaim(struct spn_device * const            device,
                               struct spn_handle_pool * const       handle_pool,
                               spn_handle_pool_reclaim_type_e const reclaim_type,
                               spn_handle_t const                   handle)
{
  struct spn_handle_pool_br * const reclaim = handle_pool->wip.reclaim + reclaim_type;

  if (reclaim->rem == 0)
    {
      reclaim->block = spn_device_handle_pool_block_reclaim_pop(device, handle_pool);
      reclaim->rem   = handle_pool->block.size;
    }

  reclaim->rem -= 1;

  uint32_t const handle_idx = reclaim->block * handle_pool->block.size + reclaim->rem;

  spn_handle_t * const handles = handle_pool->handle.extent + handle_idx;

  *handles = handle;

  if (reclaim->rem == 0)
    {
      VkCommandBuffer cb = spn_device_cb_acquire_begin(device);

      //
      // bind descriptor set, push constants and pipeline
      //
      if (reclaim_type == SPN_HANDLE_POOL_RECLAIM_TYPE_PATH)
        {
          spn_device_bind_paths_reclaim(device, handles, cb);
        }
      else
        {
          spn_device_bind_rasters_reclaim(device, handles, cb);
        }

      //
      // on completion:
      //
      // - return reclamation descriptor set
      // - return block index to handle pool
      //
      struct spn_handle_pool_reclaim_complete_payload payload = {.handle_pool = handle_pool,
                                                                 .block       = reclaim->block};

      //
      // submit the command buffer
      //
      VkFence const fence = spn_device_cb_end_fence_acquire(device,
                                                            cb,
                                                            spn_handle_pool_reclaim_complete,
                                                            &payload,
                                                            sizeof(payload));
      // boilerplate submit
      struct VkSubmitInfo const si = {.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                      .pNext                = NULL,
                                      .waitSemaphoreCount   = 0,
                                      .pWaitSemaphores      = NULL,
                                      .pWaitDstStageMask    = NULL,
                                      .commandBufferCount   = 1,
                                      .pCommandBuffers      = &cb,
                                      .signalSemaphoreCount = 0,
                                      .pSignalSemaphores    = NULL};

      vk(QueueSubmit(spn_device_queue_next(device), 1, &si, fence));
    }
}

//
//
//

void
spn_device_handle_pool_acquire(struct spn_device * const device, spn_handle_t * const handle)
{
  //
  // FIXME -- running out of handles is almost always going to be
  // fatal.  Think about how to surface this situation or simply kill
  // the device... it's probably best to invoke spn_device_lost().
  //
  struct spn_handle_pool * const handle_pool = device->handle_pool;

  if (handle_pool->wip.acquire.rem == 0)
    {
      handle_pool->wip.acquire.block =
        spn_device_handle_pool_block_acquire_pop(device, handle_pool);
      handle_pool->wip.acquire.rem = handle_pool->block.size;
    }

  if (--handle_pool->wip.acquire.rem == 0)
    {
      spn_device_handle_pool_block_reclaim_push(handle_pool, handle_pool->wip.acquire.block);
    }

  uint32_t const handle_idx =
    handle_pool->wip.acquire.block * handle_pool->block.size + handle_pool->wip.acquire.rem;

  *handle = handle_pool->handle.extent[handle_idx];

  handle_pool->handle.refcnts[*handle] = (union spn_handle_refcnt){.h = 1, .d = 1};
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
// After validation, retain the handles for the host
//

static spn_result
spn_handle_pool_validate_retain_h(struct spn_handle_pool * const   handle_pool,
                                  spn_typed_handle_type_e const    handle_type,
                                  spn_typed_handle_t const * const typed_handles,
                                  uint32_t const                   count)
{
  //
  // FIXME -- test to make sure handles aren't completely out of range integers
  //

  union spn_handle_refcnt * const refcnts      = handle_pool->handle.refcnts;
  uint32_t const                  handle_count = handle_pool->handle.count;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_typed_handle_t const typed_handle = typed_handles[ii];

      if (!SPN_TYPED_HANDLE_IS_TYPE(typed_handle, handle_type))
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          spn_handle_t const handle = SPN_TYPED_HANDLE_TO_HANDLE(typed_handle);

          if (handle < handle_count)
            {
              union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
              uint32_t const                  host       = refcnt_ptr->h;

              if (host == 0)
                {
                  return SPN_ERROR_HANDLE_INVALID;
                }
              else if (host == SPN_HANDLE_REFCNT_HOST_MAX)
                {
                  return SPN_ERROR_HANDLE_OVERFLOW;
                }
            }
          else
            {
              return SPN_ERROR_HANDLE_INVALID;
            }
        }
    }

  //
  // all the handles validated, so retain them all..
  //
  for (uint32_t ii = 0; ii < count; ii++)
    refcnts[SPN_TYPED_HANDLE_TO_HANDLE(typed_handles[ii])].h++;

  return SPN_SUCCESS;
}

//
//
//

spn_result
spn_device_handle_pool_validate_retain_h_paths(struct spn_device * const device,
                                               spn_path_t const * const  typed_handles,
                                               uint32_t const            count)
{
  return spn_handle_pool_validate_retain_h(device->handle_pool,
                                           SPN_TYPED_HANDLE_TYPE_PATH,
                                           typed_handles,
                                           count);
}

spn_result
spn_device_handle_pool_validate_retain_h_rasters(struct spn_device * const device,
                                                 spn_path_t const * const  typed_handles,
                                                 uint32_t const            count)
{
  return spn_handle_pool_validate_retain_h(device->handle_pool,
                                           SPN_TYPED_HANDLE_TYPE_RASTER,
                                           typed_handles,
                                           count);
}

//
// Validate host-provided handles before releasing.
//
// Release validation consists of:
//
//   - correct handle type
//   - handle is in range of pool
//   - host refcnt is not zero
//
// After validation, release the handles for the host
//

static spn_result
spn_device_handle_pool_validate_release_h(struct spn_device * const            device,
                                          spn_typed_handle_type_e const        handle_type,
                                          spn_handle_pool_reclaim_type_e const reclaim_type,
                                          spn_typed_handle_t const * const     typed_handles,
                                          uint32_t const                       count)
{
  struct spn_handle_pool * const  handle_pool  = device->handle_pool;
  union spn_handle_refcnt * const refcnts      = handle_pool->handle.refcnts;
  uint32_t const                  handle_count = handle_pool->handle.count;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_typed_handle_t const typed_handle = typed_handles[ii];

      if (!SPN_TYPED_HANDLE_IS_TYPE(typed_handle, handle_type))
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          spn_handle_t const handle = SPN_TYPED_HANDLE_TO_HANDLE(typed_handle);

          if (handle >= handle_count)
            {
              return SPN_ERROR_HANDLE_INVALID;
            }
          else
            {
              union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
              uint32_t const                  host       = refcnt_ptr->h;

              if (host == 0)
                {
                  return SPN_ERROR_HANDLE_INVALID;
                }
            }
        }
    }

  //
  // all the handles validated, so release them all..
  //
  // FIXME -- change this loop to fill reclaim block directly
  //
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const              handle     = SPN_TYPED_HANDLE_TO_HANDLE(typed_handles[ii]);
      union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
      union spn_handle_refcnt         refcnt     = *refcnt_ptr;

      refcnt.h -= 1;
      *refcnt_ptr = refcnt;

      if (refcnt.hd == 0)
        {
          spn_device_handle_pool_reclaim(device, handle_pool, reclaim_type, handle);
        }
    }

  return SPN_SUCCESS;
}

//
//
//

spn_result
spn_device_handle_pool_validate_release_h_paths(struct spn_device * const device,
                                                spn_path_t const * const  typed_handles,
                                                uint32_t const            count)
{
  return spn_device_handle_pool_validate_release_h(device,
                                                   SPN_TYPED_HANDLE_TYPE_PATH,
                                                   SPN_HANDLE_POOL_RECLAIM_TYPE_PATH,
                                                   typed_handles,
                                                   count);
}

spn_result
spn_device_handle_pool_validate_release_h_rasters(struct spn_device * const  device,
                                                  spn_raster_t const * const typed_handles,
                                                  uint32_t const             count)
{
  return spn_device_handle_pool_validate_release_h(device,
                                                   SPN_TYPED_HANDLE_TYPE_RASTER,
                                                   SPN_HANDLE_POOL_RECLAIM_TYPE_RASTER,
                                                   typed_handles,
                                                   count);
}

//
// After validation, retain the handles for the device
//

static void
spn_handle_pool_retain_d(struct spn_handle_pool * const   handle_pool,
                         spn_typed_handle_t const * const typed_handles,
                         uint32_t const                   count)
{
  union spn_handle_refcnt * const refcnts = handle_pool->handle.refcnts;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      refcnts[SPN_TYPED_HANDLE_TO_HANDLE(typed_handles[ii])].d++;
    }
}

void
spn_device_handle_pool_retain_d(struct spn_device * const        device,
                                spn_typed_handle_t const * const typed_handles,
                                uint32_t const                   count)
{
  spn_handle_pool_retain_d(device->handle_pool, typed_handles, count);
}

//
// Validate host-provided handles before retaining on the device.
//
//   - correct handle type
//   - handle is in range of pool
//   - host refcnt is not zero
//   - device refcnt is not at the maximum value
//

spn_result
spn_device_handle_pool_validate_retain_d(struct spn_device * const        device,
                                         spn_typed_handle_type_e const    handle_type,
                                         spn_typed_handle_t const * const typed_handles,
                                         uint32_t const                   count)
{
  struct spn_handle_pool * const  handle_pool  = device->handle_pool;
  union spn_handle_refcnt * const refcnts      = handle_pool->handle.refcnts;
  uint32_t const                  handle_count = handle_pool->handle.count;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_typed_handle_t const typed_handle = typed_handles[ii];

      if (!SPN_TYPED_HANDLE_IS_TYPE(typed_handle, handle_type))
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          spn_handle_t const handle = SPN_TYPED_HANDLE_TO_HANDLE(typed_handle);

          if (handle >= handle_count)
            {
              return SPN_ERROR_HANDLE_INVALID;
            }
          else
            {
              union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
              union spn_handle_refcnt         refcnt     = *refcnt_ptr;

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
    }

  spn_handle_pool_retain_d(handle_pool, typed_handles, count);

  return SPN_SUCCESS;
}

//
// Release the pre-validated device-held handles
//

static void
spn_device_handle_pool_release_d(struct spn_device * const            device,
                                 spn_handle_pool_reclaim_type_e const reclaim_type,
                                 spn_handle_t const * const           handles,
                                 uint32_t const                       count)
{
  struct spn_handle_pool * const  handle_pool = device->handle_pool;
  union spn_handle_refcnt * const refcnts     = handle_pool->handle.refcnts;

  //
  // FIXME -- change this loop to fill reclaim block directly
  //
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const              handle     = handles[ii];
      union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
      union spn_handle_refcnt         refcnt     = *refcnt_ptr;

      refcnt.d -= 1;
      *refcnt_ptr = refcnt;

      if (refcnt.hd == 0)
        {
          spn_device_handle_pool_reclaim(device, handle_pool, reclaim_type, handle);
        }
    }
}

//
// Untyped handle release -- no tag bit
//

void
spn_device_handle_pool_release_d_paths(struct spn_device * const  device,
                                       spn_handle_t const * const handles,
                                       uint32_t const             count)
{
  spn_device_handle_pool_release_d(device, SPN_HANDLE_POOL_RECLAIM_TYPE_PATH, handles, count);
}

void
spn_device_handle_pool_release_d_rasters(struct spn_device * const  device,
                                         spn_handle_t const * const handles,
                                         uint32_t const             count)
{
  spn_device_handle_pool_release_d(device, SPN_HANDLE_POOL_RECLAIM_TYPE_RASTER, handles, count);
}

//
//
//

void
spn_device_handle_pool_release_ring_d_paths(struct spn_device * const  device,
                                            spn_handle_t const * const paths,
                                            uint32_t const             size,
                                            uint32_t const             span,
                                            uint32_t const             head)
{
  uint32_t const count_lo = MIN_MACRO(uint32_t, head + span, size) - head;

  spn_device_handle_pool_release_d_paths(device, paths + head, count_lo);
  if (span > count_lo)
    {
      uint32_t const count_hi = span - count_lo;

      spn_device_handle_pool_release_d_paths(device, paths, count_hi);
    }
}

void
spn_device_handle_pool_release_ring_d_rasters(struct spn_device * const  device,
                                              spn_handle_t const * const rasters,
                                              uint32_t const             size,
                                              uint32_t const             span,
                                              uint32_t const             head)
{
  uint32_t const count_lo = MIN_MACRO(uint32_t, head + span, size) - head;

  spn_device_handle_pool_release_d_rasters(device, rasters + head, count_lo);
  if (span > count_lo)
    {
      uint32_t const count_hi = span - count_lo;

      spn_device_handle_pool_release_d_rasters(device, rasters, count_hi);
    }
}

//
//
//
