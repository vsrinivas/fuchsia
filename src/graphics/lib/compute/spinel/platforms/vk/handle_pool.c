// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "handle_pool.h"

#include <stdio.h>
#include <string.h>

#include "block_pool.h"
#include "common/macros.h"
#include "common/vk/assert.h"
#include "core_vk.h"
#include "device.h"
#include "dispatch.h"
#include "queue_pool.h"
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
  struct
  {
    spn_handle_t *            extent;  // array of individual host handles -- segmented into blocks
    union spn_handle_refcnt * refcnts; // array of reference counts indexed by a handle
    uint32_t                  count;   // total number of handles
  } handle;

  struct
  {
    uint32_t *                indices; // block indices
    uint32_t                  size;    // number of handles in a block

    uint32_t                  count;   // total number of indices

    struct
    {
      uint32_t                avail;   // blocks with handles
      uint32_t                empty;   // blocks with no handles
    } rem;

  } block;

  struct
  {
    struct spn_handle_pool_br acquire;
    struct spn_handle_pool_br reclaim[SPN_HANDLE_POOL_RECLAIM_TYPE_COUNT];
    //
    // FIXME(allanmac): see below -- verify pading of the reclaim[]
    // array
    //
  } wip;
};

//
// make sure these sizes match
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
// Maximum reclamation size in bytes
//

#define SPN_HANDLE_POOL_MAX_PUSH_SIZE  256

//
// clang-format on
//

//
// Sanity check for push constants
//

#ifndef NDEBUG

void
spn_device_handle_pool_assert_reclaim_size(struct spn_vk_target_config const * const config)
{
  // make sure these remain the same
  uint32_t const push_size_paths   = config->p.push_sizes.named.paths_reclaim;
  uint32_t const push_size_rasters = config->p.push_sizes.named.rasters_reclaim;

  // double-check these two sizes match
  assert(push_size_paths == push_size_rasters);

  // double-check they're less than the constant
  assert(push_size_paths <= SPN_HANDLE_POOL_MAX_PUSH_SIZE);

  // reclaim size matches the push constant size
  uint32_t const reclaim_size =
    (push_size_paths - OFFSETOF_MACRO(struct spn_vk_push_paths_reclaim, path_ids)) /
    sizeof(spn_handle_t);

  assert(reclaim_size == config->reclaim.size.paths);
  assert(reclaim_size == config->reclaim.size.rasters);
}

#endif

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

  struct spn_vk_target_config const * const config = spn_vk_get_config(device->instance);

#ifndef NDEBUG
  spn_device_handle_pool_assert_reclaim_size(config);
#endif

  uint32_t const reclaim_size = config->reclaim.size.paths;
  uint32_t const blocks       = (handle_count + reclaim_size - 1) / reclaim_size;

  //
  // FIXME(allanmac): the "block pad" may necessary for this allocator.
  // I'll revisit this once this code is heavily exercised during
  // integration testing.  Verify that have one extra block per reclaim
  // type is enough.
  //
  uint32_t const blocks_padded = blocks + SPN_HANDLE_POOL_RECLAIM_TYPE_COUNT;

  uint32_t const handles        = reclaim_size * blocks;
  uint32_t const handles_padded = reclaim_size * blocks_padded;

  //
  // allocate handle extent with padding -- (handles_padded >= handles)
  //
  handle_pool->handle.extent =
    spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  handles_padded * sizeof(*handle_pool->handle.extent));

  // initialize handles -- (handles <= handles_padded)
  for (uint32_t ii = 0; ii < handles; ii++)
    {
      handle_pool->handle.extent[ii] = ii;
    }

  //
  // allocate refcnts
  //
  size_t const size_refcnts = handles * sizeof(*handle_pool->handle.refcnts);

  handle_pool->handle.refcnts = spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                                              SPN_MEM_FLAGS_READ_WRITE,
                                                              size_refcnts);
  // zero refcnts
  memset(handle_pool->handle.refcnts, 0, size_refcnts);

  //
  // save the count
  //
  handle_pool->handle.count = handles;

  //
  // allocate blocks of handles
  //
  size_t const size_indices = blocks_padded * sizeof(*handle_pool->block.indices);

  handle_pool->block.indices = spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                                             SPN_MEM_FLAGS_READ_WRITE,
                                                             size_indices);

  // initialize block accounting
  for (uint32_t ii = 0; ii < blocks_padded; ii++)
    {
      handle_pool->block.indices[ii] = ii;
    }

  handle_pool->block.size      = reclaim_size;  // reclaim size for both paths and rasters
  handle_pool->block.count     = blocks_padded;
  handle_pool->block.rem.avail = blocks;
  handle_pool->block.rem.empty = blocks_padded - blocks;

  handle_pool->wip.acquire.rem = 0;

  // initialize reclaim/acquire
  for (uint32_t ii = 0; ii < SPN_HANDLE_POOL_RECLAIM_TYPE_COUNT; ii++)
    handle_pool->wip.reclaim[ii].rem = 0;
}

//
//
//

uint32_t
spn_device_handle_pool_get_allocated_handle_count(struct spn_device * const device)
{
  return device->handle_pool->handle.count;
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
      SPN_DEVICE_WAIT(device);
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

  //
  // FIXME(allanmac): Pretty sure we will (1) never wait and (2) never
  // want to wait here.  So remove this and ensure there are always
  // enough blocks.
  //
  while ((empty = handle_pool->block.rem.empty) == 0)
    {
      SPN_DEVICE_WAIT(device);
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

#if !defined(NDEBUG) && 0
#define SPN_HANDLE_POOL_DEBUG
#endif

//
//
//

struct spn_handle_pool_reclaim_complete_payload
{
  struct spn_handle_pool * handle_pool;
  uint32_t                 block;

#ifdef SPN_HANDLE_POOL_DEBUG
  spn_handle_pool_reclaim_type_e reclaim_type;
#endif
};

//
//
//

static void
spn_handle_pool_reclaim_complete(void * const pfn_payload)
{
  struct spn_handle_pool_reclaim_complete_payload const * const payload     = pfn_payload;
  struct spn_handle_pool * const                                handle_pool = payload->handle_pool;

#ifdef SPN_HANDLE_POOL_DEBUG
  static const char * const reclaim_type_to_str[] = {
    STRINGIFY_MACRO(SPN_HANDLE_POOL_RECLAIM_TYPE_PATH),
    STRINGIFY_MACRO(SPN_HANDLE_POOL_RECLAIM_TYPE_RASTER)
  };
  fprintf(stderr, "%s : %s\n", __func__, reclaim_type_to_str[payload->reclaim_type]);
#endif

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
spn_device_bind_paths_reclaim(struct spn_device * const            device,
                              struct spn_handle_pool const * const handle_pool,
                              spn_handle_t const * const           handles,
                              VkCommandBuffer                      cb)
{
  struct spn_vk * const               instance = device->instance;
  struct spn_vk_ds_block_pool_t const ds       = spn_device_block_pool_get_ds(device);

  spn_vk_ds_bind_paths_reclaim_block_pool(instance, cb, ds);

  union
  {
    struct spn_vk_push_paths_reclaim reclaim;
    uint8_t                          bytes[SPN_HANDLE_POOL_MAX_PUSH_SIZE];
  } push;

  push.reclaim.bp_mask = spn_device_block_pool_get_mask(device);

  //
  // FIXME -- any way to avoid this copy?  Only if the push constant
  // structure mirrored the reclamation structure so probably not.
  //
  memcpy(push.reclaim.path_ids, handles, sizeof(*handles) * handle_pool->block.size);

  spn_vk_p_push_paths_reclaim(instance, cb, &push.reclaim);

  spn_vk_p_bind_paths_reclaim(instance, cb);
}

static void
spn_device_bind_rasters_reclaim(struct spn_device * const            device,
                                struct spn_handle_pool const * const handle_pool,
                                spn_handle_t const * const           handles,
                                VkCommandBuffer                      cb)
{
  struct spn_vk * const               instance = device->instance;
  struct spn_vk_ds_block_pool_t const ds       = spn_device_block_pool_get_ds(device);

  spn_vk_ds_bind_rasters_reclaim_block_pool(instance, cb, ds);

  union
  {
    struct spn_vk_push_rasters_reclaim reclaim;
    uint8_t                            bytes[SPN_HANDLE_POOL_MAX_PUSH_SIZE];
  } push;

  push.reclaim.bp_mask = spn_device_block_pool_get_mask(device);

  memcpy(push.reclaim.raster_ids, handles, sizeof(*handles) * handle_pool->block.size);

  spn_vk_p_push_rasters_reclaim(instance, cb, &push.reclaim);

  spn_vk_p_bind_rasters_reclaim(instance, cb);
}

//
// FIXME(allanmac): make the reclamation API hand over a pointer to the
// entire reclamation block instead of adding a handle one at a time.
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
      //
      // acquire a dispatch id
      //
      bool const is_path = (reclaim_type == SPN_HANDLE_POOL_RECLAIM_TYPE_PATH);

      spn_dispatch_stage_e const stage =
        is_path ? SPN_DISPATCH_STAGE_RECLAIM_PATHS : SPN_DISPATCH_STAGE_RECLAIM_RASTERS;

      spn_dispatch_id_t id;

      spn(device_dispatch_acquire(device, stage, &id));

      //
      // bind descriptor set, push constants and pipeline
      //
      VkCommandBuffer cb = spn_device_dispatch_get_cb(device, id);

      if (reclaim_type == SPN_HANDLE_POOL_RECLAIM_TYPE_PATH)
        {
          spn_device_bind_paths_reclaim(device, handle_pool, handles, cb);
        }
      else
        {
          spn_device_bind_rasters_reclaim(device, handle_pool, handles, cb);
        }

      // dispatch one workgroup per reclamation block
      vkCmdDispatch(cb, 1, 1, 1);

      //
      // on completion:
      //
      // - return reclamation descriptor set
      // - return block index to handle pool
      //
      struct spn_handle_pool_reclaim_complete_payload * const payload =
        spn_device_dispatch_set_completion(device,
                                           id,
                                           spn_handle_pool_reclaim_complete,
                                           sizeof(*payload));

      payload->handle_pool = handle_pool;
      payload->block       = reclaim->block;

#ifdef SPN_HANDLE_POOL_DEBUG
      payload->reclaim_type = reclaim_type;
#endif

      //
      // submit the dispatch
      //
      spn_device_dispatch_submit(device, id);

#ifdef SPN_HANDLE_POOL_DEBUG
      fprintf(stderr, "%s\n", __func__);
      spn(device_wait_all(device, true));
#endif
    }
}

//
//
//

void
spn_device_handle_pool_acquire(struct spn_device * const device, spn_handle_t * const handle)
{
  //
  // FIXME(allanmac): running out of handles is almost always going to
  // be *fatal*.  Think about how to surface this situation or simply
  // kill the device... it's probably best to invoke spn_device_lost().
  //
  struct spn_handle_pool * const handle_pool = device->handle_pool;

  // need a new block of handles?
  if (handle_pool->wip.acquire.rem == 0)
    {
      handle_pool->wip.acquire.block =
        spn_device_handle_pool_block_acquire_pop(device, handle_pool);

      handle_pool->wip.acquire.rem = handle_pool->block.size;
    }

  // pop handle from block
  handle_pool->wip.acquire.rem -= 1;

  uint32_t const handle_idx =
    handle_pool->wip.acquire.block * handle_pool->block.size + handle_pool->wip.acquire.rem;

  *handle = handle_pool->handle.extent[handle_idx];

  handle_pool->handle.refcnts[*handle] = (union spn_handle_refcnt){ .h = 1, .d = 1 };

  // if the block is empty, put it on the reclamation stack
  if (handle_pool->wip.acquire.rem == 0)
    {
      spn_device_handle_pool_block_reclaim_push(handle_pool, handle_pool->wip.acquire.block);
    }
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

static spn_result_t
spn_device_handle_pool_validate_retain_h(struct spn_device * const  device,
                                         spn_handle_t const * const handles,
                                         uint32_t                   count)
{
  struct spn_handle_pool * const  handle_pool = device->handle_pool;
  union spn_handle_refcnt * const refcnts     = handle_pool->handle.refcnts;
  uint32_t const                  handle_max  = handle_pool->handle.count;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      if (handle >= handle_max)
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
          union spn_handle_refcnt const   refcnt     = *refcnt_ptr;

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
spn_device_handle_pool_validate_release_h(struct spn_device * const            device,
                                          spn_handle_t const * const           handles,
                                          uint32_t                             count,
                                          spn_handle_pool_reclaim_type_e const reclaim_type)
{
  struct spn_handle_pool * const  handle_pool = device->handle_pool;
  union spn_handle_refcnt * const refcnts     = handle_pool->handle.refcnts;
  uint32_t const                  handle_max  = handle_pool->handle.count;

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
          union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
          union spn_handle_refcnt const   refcnt     = *refcnt_ptr;

          if (refcnt.h == 0)
            {
              return SPN_ERROR_HANDLE_INVALID;
            }
        }
    }

  //
  // all the handles validated, so release them all..
  //
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      refcnts[handle].h--;
    }

  //
  // ... reclaim any that were zero -- this may block/spin
  //
  // TODO(allanmac): spn_device_handle_pool_reclaim(handles[])
  //
  //
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      if (refcnts[handle].hd == 0)
        {
          spn_device_handle_pool_reclaim(device, handle_pool, reclaim_type, handle);
        }
    }

  return SPN_SUCCESS;
}

spn_result_t
spn_device_handle_pool_validate_release_h_paths(struct spn_device * const     device,
                                                struct spn_path const * const paths,
                                                uint32_t const                count)
{
  union spn_paths_to_handles const p2h = { paths };

  return spn_device_handle_pool_validate_release_h(device,
                                                   p2h.handles,
                                                   count,
                                                   SPN_HANDLE_POOL_RECLAIM_TYPE_PATH);
}

spn_result_t
spn_device_handle_pool_validate_release_h_rasters(struct spn_device * const       device,
                                                  struct spn_raster const * const rasters,
                                                  uint32_t const                  count)
{
  union spn_rasters_to_handles const r2h = { rasters };

  return spn_device_handle_pool_validate_release_h(device,
                                                   r2h.handles,
                                                   count,
                                                   SPN_HANDLE_POOL_RECLAIM_TYPE_RASTER);
}

//
// Validate host-provided handles before retaining on the device.
//
//   - handle is in range of pool
//   - host refcnt is not zero
//   - device refcnt is not at the maximum value
//

static spn_result_t
spn_device_handle_pool_validate_d(struct spn_device * const  device,
                                  spn_handle_t const * const handles,
                                  uint32_t const             count)
{
  struct spn_handle_pool * const  handle_pool = device->handle_pool;
  union spn_handle_refcnt * const refcnts     = handle_pool->handle.refcnts;
  uint32_t const                  handle_max  = handle_pool->handle.count;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      if (handle >= handle_max)
        {
          return SPN_ERROR_HANDLE_INVALID;
        }
      else
        {
          union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
          union spn_handle_refcnt const   refcnt     = *refcnt_ptr;

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

  return spn_device_handle_pool_validate_d(device, p2h.handles, count);
}

spn_result_t
spn_device_handle_pool_validate_d_rasters(struct spn_device * const       device,
                                          struct spn_raster const * const rasters,
                                          uint32_t const                  count)
{
  union spn_rasters_to_handles const r2h = { rasters };

  return spn_device_handle_pool_validate_d(device, r2h.handles, count);
}

//
// After validation, retain the handles for the device
//

static void
spn_device_handle_pool_retain_d(struct spn_device * const  device,
                                spn_handle_t const * const handles,
                                uint32_t const             count)

{
  struct spn_handle_pool * const  handle_pool = device->handle_pool;
  union spn_handle_refcnt * const refcnts     = handle_pool->handle.refcnts;

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
  // TODO(allanmac): Change this loop to fill reclaim block directly
  // to save a bunch of cycles.
  //
  // TODO(allanmac): In a future CL, evaluate if using separate
  // iterations for invalidating the timeline events and decrementing
  // the device-side count is a more performant approach.
  //
  // For now, let's keep it simple until we've integrated.
  //
  for (uint32_t ii = 0; ii < count; ii++)
    {
      spn_handle_t const handle = handles[ii];

      //
      // decrement the handle's device-side count
      //
      union spn_handle_refcnt * const refcnt_ptr = refcnts + handle;
      union spn_handle_refcnt         refcnt     = *refcnt_ptr;

      refcnt.d--;

      *refcnt_ptr = refcnt;

      //
      // reclaim the handle?
      //
      if (refcnt.hd == 0)
        {
          spn_device_handle_pool_reclaim(device, handle_pool, reclaim_type, handle);
        }
    }
}

//
// Release device-held spans of handles of known type
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
// Release handles on a ring -- up to two spans
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
