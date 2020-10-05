// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "path_builder_impl.h"

#include <float.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include "block_pool.h"
#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "device.h"
#include "dispatch.h"
#include "handle_pool.h"
#include "queue_pool.h"
#include "ring.h"
#include "spinel_assert.h"
#include "trace.h"
#include "vk_target.h"

//
// The path builder moves bulk path data, nodes and a single header
// from the host into the device-managed "block" memory pool.  The
// data is arranged into a SIMT/SIMD-friendly data structure that can
// be efficiently read by the rasterizer.
//
// A simplifying assumption is that the maximum length of a single
// path can't be larger than what fits in path builder ring.
//
// This would be a very long path and a legitimate size limitation.
//
// If a path is too long then the path builder instance is lost.
//
// Note that this restriction can be removed with added complexity to
// the builder and shader.
//
// Also note that for some systems, it may be appropriate to never
// pull path data into the device-managed block pool and instead
// present the path data to the device in a temporarily available
// allocated memory "zone" of paths that can be discarded all at once.
//
// For other systems, it may be appropriate to simply copy the path
// data from host to device.
//
// The general strategy that this particular Vulkan implementation
// uses is to allocate a large "HOST_COHERENT" bulk-data path buffer
// and an auxilary mappable command buffer.
//
// The work-in-progress path's header and latest node are updated
// locally until full and then stored because the mapped HOST_COHERENT
// memory is likely uncached and read-modify-writes will be expensive.
//
// A line/quad/cubic/rat_quad/rat_cubic acquires 4/6/8/7/10 segments
// which may be spread across one or more contiguous blocks.
//
// If a flush() occurs, then the remaining columns of multi-segment
// paths are initialized with zero-length path primitives.
//
// Every block's command word has a type and a count acquired from a
// rolling counter.
//
// Note that the maximum number of "in-flight" path copy grids is
// conveniently determined by the size of the fence pool.
//

//
// Verify the path header size
//

STATIC_ASSERT_MACRO_1(sizeof(union spn_path_header) == SPN_PATH_HEAD_DWORDS * sizeof(uint32_t));

//
// A dispatch record represents a continguous region of the ring that
// can be copied from the host to device.
//
// There should enough dispatch records available so that if they're all
// in flight then either a PCIe or memory bandwidth "roofline" limit is
// reached.
//
// The expectation is that the path builder will *not* be CPU bound.
//

struct spn_pbi_span_head
{
  uint32_t head;
  uint32_t span;
};

struct spn_pbi_dispatch
{
  struct spn_pbi_span_head blocks;
  struct spn_pbi_span_head paths;

  uint32_t          rolling;  // FIXME(allanmac): move to wip
  spn_dispatch_id_t id;       // FIXME(allanmac): move to wip

  bool complete;
};

//
//
//

struct spn_pbi_vk
{
  struct
  {
    VkDescriptorBufferInfo dbi;
    VkDeviceMemory         dm;
  } alloc;

  struct
  {
    VkDescriptorBufferInfo dbi;
    VkDeviceMemory         dm;
  } ring;
};

//
//
//

struct spn_path_builder_impl
{
  struct spn_path_builder * path_builder;
  struct spn_device *       device;
  struct spn_pbi_vk         vk;

  struct
  {
    uint32_t block_dwords;
    uint32_t block_subgroups;

    uint32_t subgroup_dwords;
    uint32_t subgroup_subblocks;

    uint32_t rolling_one;

    uint32_t eager_size;

  } config;

  //
  // block and cmd rings share a buffer
  //
  // [<--- blocks --->|<--- cmds --->]
  //
  struct
  {
    struct spn_ring ring;

    uint32_t rolling;

    struct
    {
      uint32_t rem;
      float *  f32;
    } subgroups;

    union
    {
      uint32_t * u32;
      float *    f32;
      // add head and node structures
    } blocks;

    uint32_t * cmds;
  } mapped;

  //
  // work in progress header
  //
  struct
  {
    union spn_path_header header;

    uint32_t * node;

    struct
    {
      uint32_t idx;
      uint32_t rolling;
    } head;

    struct
    {
      uint32_t rolling;
    } segs;

    uint32_t rem;
  } wip;

  //
  // Resources released upon an grid completion:
  //
  //   - Path handles are released immediately.
  //
  //   - Dispatch records and associated mapped spans are released in
  //     ring order.
  //
  // Note that there can only be as many paths as there are blocks
  // (empty paths have a header block) so this resource is implicitly
  // managed by the mapped.ring and release.dispatch.ring.
  //
  struct
  {
    spn_handle_t *  extent;
    struct spn_next next;
  } paths;

  struct
  {
    struct spn_pbi_dispatch * extent;
    struct spn_ring           ring;
  } dispatches;
};

//
//
//

static spn_result_t
spn_pbi_lost_begin(struct spn_path_builder_impl * const impl)
{
  return SPN_ERROR_PATH_BUILDER_LOST;
}

static spn_result_t
spn_pbi_lost_end(struct spn_path_builder_impl * const impl, spn_path_t * const path)
{
  *path = SPN_PATH_INVALID;

  return SPN_ERROR_PATH_BUILDER_LOST;
}

static spn_result_t
spn_pbi_release(struct spn_path_builder_impl * const impl);

static spn_result_t
spn_pbi_lost_release(struct spn_path_builder_impl * const impl)
{
  //
  // FIXME -- releasing a lost path builder might eventually require a
  // specialized function.  For now, just call the default release.
  //
  return spn_pbi_release(impl);
}

static spn_result_t
spn_pbi_lost_flush(struct spn_path_builder_impl * const impl)
{
  return SPN_ERROR_PATH_BUILDER_LOST;
}

//
// Define primitive geometry "lost" pfns
//

#define SPN_PBI_PFN_LOST_NAME(_p) spn_pbi_lost_##_p

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n)                                            \
  static spn_result_t SPN_PBI_PFN_LOST_NAME(_p)(struct spn_path_builder_impl * const impl)         \
  {                                                                                                \
    return SPN_ERROR_PATH_BUILDER_LOST;                                                            \
  }

SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()

//
// If (wip.span == mapped.ring.size) then the path is too long and the
// path builder is terminally "lost".  The path builder should be
// released and a new one created.
//

static void
spn_pbi_lost(struct spn_path_builder_impl * const impl)
{
  struct spn_path_builder * const pb = impl->path_builder;

  pb->begin   = spn_pbi_lost_begin;
  pb->end     = spn_pbi_lost_end;
  pb->release = spn_pbi_lost_release;
  pb->flush   = spn_pbi_lost_flush;

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n) pb->_p = SPN_PBI_PFN_LOST_NAME(_p);

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()
}

//
// Append path to path release extent -- note that this resource is
// implicitly "clocked" by the mapped.ring.
//

static void
spn_pbi_path_append(struct spn_path_builder_impl * const impl, spn_path_t const * const path)
{
  uint32_t const idx = spn_next_acquire_1(&impl->paths.next);

  impl->paths.extent[idx] = path->handle;
}

//
// A dispatch captures how many paths and blocks are in a dispatched or
// the work-in-progress compute grid.
//

static struct spn_pbi_dispatch *
spn_pbi_dispatch_idx(struct spn_path_builder_impl * const impl, uint32_t const idx)
{
  return impl->dispatches.extent + idx;
}

static struct spn_pbi_dispatch *
spn_pbi_dispatch_head(struct spn_path_builder_impl * const impl)
{
  return spn_pbi_dispatch_idx(impl, impl->dispatches.ring.head);
}

static struct spn_pbi_dispatch *
spn_pbi_dispatch_tail(struct spn_path_builder_impl * const impl)
{
  return spn_pbi_dispatch_idx(impl, impl->dispatches.ring.tail);
}

static void
spn_pbi_dispatch_init(struct spn_path_builder_impl * const impl,
                      struct spn_ring const * const        dispatches_ring)
{
  SPN_VK_TRACE_PATH_BUILDER_DISPATCH_ACQUIRE(impl, dispatches_ring->head);

  struct spn_pbi_dispatch * const dispatch = spn_pbi_dispatch_idx(impl, dispatches_ring->head);

  // head is the wip path's head idx
  dispatch->blocks.head = impl->wip.head.idx;
  dispatch->blocks.span = 0;

  // no paths have been appended
  dispatch->paths.head = impl->paths.next.head;
  dispatch->paths.span = 0;

  // rolling is the wip's path's rolling counter
  dispatch->rolling  = impl->wip.head.rolling;
  dispatch->complete = false;

  spn(device_dispatch_acquire(impl->device, SPN_DISPATCH_STAGE_PATH_BUILDER, &dispatch->id));

  spn_device_dispatch_set_flush_arg(impl->device, dispatch->id, impl);
}

static void
spn_pbi_dispatch_drop(struct spn_path_builder_impl * const impl)
{
  struct spn_ring * const ring = &impl->dispatches.ring;

  spn_ring_drop_1(ring);
}

static void
spn_pbi_dispatch_acquire(struct spn_path_builder_impl * const impl)
{
  struct spn_ring * const ring = &impl->dispatches.ring;

  while (spn_ring_is_empty(ring))
    {
      spn(device_wait(impl->device, __func__));
    }

  spn_pbi_dispatch_init(impl, ring);

  //
  // Verifies conservation of ring blocks
  //
#ifndef NDEBUG
  if (impl->path_builder->state == SPN_PATH_BUILDER_STATE_BUILDING)
    {
      struct spn_pbi_dispatch * const dispatch = spn_pbi_dispatch_head(impl);

      assert(dispatch->blocks.head == impl->wip.head.idx);
    }
#endif
}

static void
spn_pbi_dispatch_append(struct spn_path_builder_impl * const impl,
                        struct spn_pbi_dispatch * const      dispatch,
                        spn_path_t const * const             path)
{
  spn_pbi_path_append(impl, path);

  dispatch->blocks.span += impl->wip.header.named.blocks;
  dispatch->paths.span += 1;
}

static bool
spn_pbi_is_dispatch_empty(struct spn_pbi_dispatch const * const dispatch)
{
  return dispatch->paths.span == 0;
}

//
//
//

struct spn_pbi_complete_payload
{
  struct spn_path_builder_impl * impl;
  struct spn_vk_ds_paths_copy_t  ds_pc;
  uint32_t                       dispatch_idx;
};

//
//
//

static void
spn_pbi_complete(void * pfn_payload)
{
  struct spn_pbi_complete_payload const * const payload  = pfn_payload;
  struct spn_path_builder_impl * const          impl     = payload->impl;
  struct spn_device * const                     device   = impl->device;
  struct spn_vk * const                         instance = device->instance;

  // immediately release descriptor set
  spn_vk_ds_release_paths_copy(instance, payload->ds_pc);

  //
  // get our dispatch record
  //
  uint32_t const            dispatch_idx = payload->dispatch_idx;
  struct spn_pbi_dispatch * dispatch     = spn_pbi_dispatch_idx(impl, dispatch_idx);

  //
  // These path handles are now materialized
  //
  spn_device_dispatch_handles_complete(device,
                                       impl->paths.extent,
                                       impl->paths.next.size,
                                       dispatch->paths.head,
                                       dispatch->paths.span);

  //
  // Release the paths -- may invoke wait()
  //
  spn_device_handle_pool_release_ring_d_paths(device,
                                              impl->paths.extent,
                                              impl->paths.next.size,
                                              dispatch->paths.head,
                                              dispatch->paths.span);
  //
  // If the dispatch is the tail of the ring then try to release as
  // many dispatch records as possible...
  //
  // Note that kernels can complete in any order so the release
  // records need to add to the mapped.ring.tail in order.
  //
  if (impl->mapped.ring.tail == dispatch->blocks.head)
    {
      while (true)
        {
          // release the blocks and cmds
          spn_ring_release_n(&impl->mapped.ring, dispatch->blocks.span);

          // release the dispatch
          spn_ring_release_n(&impl->dispatches.ring, 1);

          // any dispatches in flight?
          if (spn_ring_is_full(&impl->dispatches.ring))
            break;

          dispatch = spn_pbi_dispatch_tail(impl);

          if (!dispatch->complete)
            break;
        }
    }
  else
    {
      dispatch->complete = true;
    }

  SPN_VK_TRACE_PATH_BUILDER_DISPATCH_RELEASE(impl, dispatch_idx);
}

//
//
//

spn_result_t
spn_pbi_flush(struct spn_path_builder_impl * const impl)
{
  struct spn_pbi_dispatch * const dispatch = spn_pbi_dispatch_head(impl);

  // anything to launch?
  if (spn_pbi_is_dispatch_empty(dispatch))
    return SPN_SUCCESS;

  SPN_VK_TRACE_PATH_BUILDER_DISPATCH_FLUSH(impl, impl->dispatches.ring.head);

  //
  // We're go for launch...
  //
  struct spn_device * const device = impl->device;

  // reset the flush arg associated with the dispatch id
  spn_device_dispatch_reset_flush_arg(device, dispatch->id);

  //
  // DISPATCH
  //
  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, dispatch->id);

  // the vk instance
  struct spn_vk * const instance = device->instance;

  // bind global BLOCK_POOL descriptor set
  spn_vk_ds_bind_paths_alloc_block_pool(instance, cb, spn_device_block_pool_get_ds(device));

  // acquire PATHS_COPY descriptor set
  struct spn_vk_ds_paths_copy_t ds_pc;

  spn_vk_ds_acquire_paths_copy(instance, device, &ds_pc);

  // init the dbi structs
  *spn_vk_ds_get_paths_copy_pc_alloc(instance, ds_pc) = impl->vk.alloc.dbi;
  *spn_vk_ds_get_paths_copy_pc_ring(instance, ds_pc)  = impl->vk.ring.dbi;

  // update PATHS_COPY descriptor set
  spn_vk_ds_update_paths_copy(instance, &device->environment, ds_pc);

  // bind PATHS_COPY descriptor set
  spn_vk_ds_bind_paths_alloc_paths_copy(instance, cb, ds_pc);

  //
  // Set up push constants -- note that for now the paths_copy push
  // constants are an extension of the paths_alloc constants.
  //
  // This means we can push the constants once.
  //
  struct spn_vk_push_paths_alloc const push = {

    // paths_alloc and paths_copy
    .bp_mask      = spn_device_block_pool_get_mask(device),
    .pc_alloc_idx = impl->dispatches.ring.head,
    .pc_span      = dispatch->blocks.span,

    // only paths_copy
    .pc_head    = dispatch->blocks.head,
    .pc_rolling = dispatch->rolling,
    .pc_size    = impl->mapped.ring.size
  };

  spn_vk_p_push_paths_alloc(instance, cb, &push);

  // bind the PATHS_ALLOC pipeline
  spn_vk_p_bind_paths_alloc(instance, cb);

  // dispatch the pipeline
  vkCmdDispatch(cb, 1, 1, 1);

  // compute barrier
  vk_barrier_compute_w_to_compute_r(cb);

  // bind the PATHS_COPY pipeline
  spn_vk_p_bind_paths_copy(instance, cb);

  // FIXME(allanmac): size the grid

  // dispatch the pipeline
  vkCmdDispatch(cb, dispatch->blocks.span, 1, 1);

  //
  // on completion... return resources
  //
  struct spn_pbi_complete_payload * const payload =
    spn_device_dispatch_set_completion(device,  //
                                       dispatch->id,
                                       spn_pbi_complete,
                                       sizeof(*payload));

  payload->impl         = impl;
  payload->ds_pc        = ds_pc;
  payload->dispatch_idx = impl->dispatches.ring.head;

  //
  // the current dispatch is now sealed so drop it
  //
  spn_pbi_dispatch_drop(impl);

  //
  // submit the dispatch
  //
  spn_device_dispatch_submit(device, dispatch->id);

  //
  // acquire and initialize the next dispatch
  //
  spn_pbi_dispatch_acquire(impl);

  return SPN_SUCCESS;
}

//
// Before returning a path handle, any remaining coordinates in the
// subgroups(s) are finalized with zero-length primitives.
//

static void
spn_pb_cn_coords_zero(float * coords, uint32_t rem)
{
  do
    {
      *coords++ = 0.0f;
  } while (--rem > 0);
}

static void
spn_pb_cn_coords_finalize(float * coords[], uint32_t coords_len, uint32_t const rem)
{
  do
    {
      spn_pb_cn_coords_zero(*coords++, rem);
  } while (--coords_len > 0);
}

static void
spn_pb_finalize_subgroups(struct spn_path_builder_impl * const impl)
{
  struct spn_path_builder * const pb = impl->path_builder;

  //
  // Note that this zeroes a cacheline / subblock at a time
  //
#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n)                                            \
  {                                                                                                \
    uint32_t rem = pb->cn.rem._p;                                                                  \
    if (rem > 0)                                                                                   \
      {                                                                                            \
        pb->cn.rem._p = 0;                                                                         \
        spn_pb_cn_coords_finalize(pb->cn.coords._p, _n, rem);                                      \
      }                                                                                            \
  }

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()
}

//
//
//

static void
spn_pbi_cmd_append(struct spn_path_builder_impl * const impl,
                   uint32_t const                       idx,
                   uint32_t const                       type)
{
  uint32_t const rolling = impl->mapped.rolling;
  uint32_t const cmd     = rolling | type;

  impl->mapped.cmds[idx] = cmd;
  impl->mapped.rolling   = rolling + impl->config.rolling_one;

  impl->wip.header.named.blocks += 1;
}

//
//
//

static void
spn_pbi_node_append_next(struct spn_path_builder_impl * const impl)
{
  // no need to increment the node pointer
  *impl->wip.node = impl->mapped.rolling | SPN_BLOCK_ID_TAG_PATH_NEXT;
}

//
//
//

static uint32_t
spn_pbi_acquire_head_block(struct spn_path_builder_impl * const impl)
{
  struct spn_ring * const ring = &impl->mapped.ring;

  if (spn_ring_is_empty(ring))
    {
      spn_pbi_flush(impl);  // launch whatever is in the ring

      do
        {
          spn(device_wait(impl->device, __func__));
      } while (spn_ring_is_empty(ring));
    }

  return spn_ring_acquire_1(&impl->mapped.ring);
}

static spn_result_t
spn_pbi_acquire_node_segs_block(struct spn_path_builder_impl * const impl, uint32_t * const idx)
{
  struct spn_ring * const ring = &impl->mapped.ring;

  if (spn_ring_is_empty(ring))
    {
      //
      // If the work in progress is going to exceed the size of the ring
      // then this is a fatal error. At this point, we can kill the path
      // builder instead of the device.
      //
      if (impl->wip.header.named.blocks >= impl->mapped.ring.size)
        {
          spn_pbi_lost(impl);

          return SPN_ERROR_PATH_BUILDER_LOST;  // FIXME(allanmac): return a "TOO_LONG" error?
        }

      //
      // Otherwise, launch whatever is in the ring...
      //
      spn_pbi_flush(impl);

      //
      // ... and wait for blocks to appear in the ring!
      //
      do
        {
          spn(device_wait(impl->device, __func__));
      } while (spn_ring_is_empty(ring));
    }

  *idx = spn_ring_acquire_1(&impl->mapped.ring);

  return SPN_SUCCESS;
}

//
//
//

static void
spn_pbi_acquire_head(struct spn_path_builder_impl * const impl)
{
  uint32_t const idx = spn_pbi_acquire_head_block(impl);

  spn_pbi_cmd_append(impl, idx, SPN_PATHS_COPY_CMD_TYPE_HEAD);

  uint32_t const   offset = idx * impl->config.block_dwords;
  uint32_t * const head   = impl->mapped.blocks.u32 + offset;

  impl->wip.node = head + SPN_PATH_HEAD_DWORDS;
  impl->wip.rem  = impl->config.block_dwords - SPN_PATH_HEAD_DWORDS;
}

static spn_result_t
spn_pbi_acquire_node(struct spn_path_builder_impl * const impl)
{
  spn_pbi_node_append_next(impl);

  uint32_t           idx;
  spn_result_t const err = spn_pbi_acquire_node_segs_block(impl, &idx);

  if (err != SPN_SUCCESS)
    return err;

  spn_pbi_cmd_append(impl, idx, SPN_PATHS_COPY_CMD_TYPE_NODE);

  impl->wip.header.named.nodes += 1;

  uint32_t const offset = idx * impl->config.block_dwords;

  impl->wip.node = impl->mapped.blocks.u32 + offset;
  impl->wip.rem  = impl->config.block_dwords;

  return SPN_SUCCESS;
}

static spn_result_t
spn_pbi_acquire_segs(struct spn_path_builder_impl * const impl)
{
  uint32_t idx;

  spn_result_t const err = spn_pbi_acquire_node_segs_block(impl, &idx);

  if (err != SPN_SUCCESS)
    return err;

  impl->wip.segs.rolling = impl->mapped.rolling;

  spn_pbi_cmd_append(impl, idx, SPN_PATHS_COPY_CMD_TYPE_SEGS);

  uint32_t const offset = idx * impl->config.block_dwords;

  impl->mapped.subgroups.f32 = impl->mapped.blocks.f32 + offset;
  impl->mapped.subgroups.rem = impl->config.block_subgroups;

  return SPN_SUCCESS;
}

//
//
//

static void
spn_pbi_node_append_segs(struct spn_path_builder_impl * const impl, uint32_t const tag)
{
  uint32_t const subgroup_idx = impl->config.block_subgroups - impl->mapped.subgroups.rem;
  uint32_t const subblock_idx = subgroup_idx * impl->config.subgroup_subblocks;
  uint32_t const subblock_shl = subblock_idx << SPN_TAGGED_BLOCK_ID_BITS_TAG;
  uint32_t const tbid         = (impl->wip.segs.rolling | subblock_shl | tag);

  *impl->wip.node++ = tbid;

  impl->wip.rem -= 1;
}

//
//
//

static spn_result_t
spn_pbi_prim_acquire_subgroups(struct spn_path_builder_impl * const impl,
                               uint32_t const                       tag,
                               float **                             coords,
                               uint32_t                             coords_len)
{
  //
  // Write a tagged block id to the node that records:
  //
  //   { block id, subblock idx, prim tag }
  //
  // If the path primitive spans more than one block then there will
  // be a TAG_PATH_NEXT pointing to the next block.
  //
  // Note that a subgroup may be 1, 2 or a higher power of two
  // subblocks.
  //
  uint32_t curr_tag = tag;

  do
    {
      // is there only one tagged block id left in the node?
      if (impl->wip.rem == 1)
        {
          spn_result_t const err = spn_pbi_acquire_node(impl);

          if (err != SPN_SUCCESS)
            return err;
        }

      // are there no subgroups left?
      if (impl->mapped.subgroups.rem == 0)
        {
          spn_result_t const err = spn_pbi_acquire_segs(impl);

          if (err != SPN_SUCCESS)
            return err;
        }

      // record the tagged block id
      spn_pbi_node_append_segs(impl, curr_tag);

      // any tag after this is a caboose
      curr_tag = SPN_BLOCK_ID_TAG_PATH_NEXT;

      // initialize path builder's pointers
      uint32_t count = MIN_MACRO(uint32_t, coords_len, impl->mapped.subgroups.rem);

      impl->mapped.subgroups.rem -= count;

      coords_len -= count;

      do
        {
          *coords++ = impl->mapped.subgroups.f32;

          impl->mapped.subgroups.f32 += impl->config.subgroup_dwords;
      } while (--count > 0);
  } while (coords_len > 0);

  // update path builder rem count
  impl->path_builder->cn.rem.aN[tag] = impl->config.subgroup_dwords;

  // the prims count tracks the number of tagged block ids
  impl->wip.header.named.prims.array[tag] += 1;

  return SPN_SUCCESS;
}

//
// Define primitive geometry pfns
//

#define SPN_PBI_PFN_NAME(_p) spn_pbi_##_p

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n)                                            \
  static spn_result_t SPN_PBI_PFN_NAME(_p)(struct spn_path_builder_impl * const impl)              \
  {                                                                                                \
    return spn_pbi_prim_acquire_subgroups(impl, _i, impl->path_builder->cn.coords._p, _n);         \
  }

SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()

//
//
//

STATIC_ASSERT_MACRO_1(sizeof(union spn_path_header) ==
                      MEMBER_SIZE_MACRO(union spn_path_header, array));

static void
spn_pbi_wip_reset(struct spn_path_builder_impl * const impl)
{
  struct spn_path_builder * const pb = impl->path_builder;

  // init path builder counters
#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n) pb->cn.rem._p = 0;

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND();

  // there are no subblocks available
  impl->mapped.subgroups.rem = 0;

  // update header -- don't bother initializing .handle and .na
  impl->wip.header.named.blocks = 0;
  impl->wip.header.named.nodes  = 0;

  // save mapped head to wip
  impl->wip.head.idx     = impl->mapped.ring.head;
  impl->wip.head.rolling = impl->mapped.rolling;

  // reset prim counters
  memset(impl->wip.header.named.prims.array, 0, sizeof(impl->wip.header.named.prims.array));

  // reset bounds
  impl->wip.header.named.bounds = (struct spn_vec4){ +FLT_MIN, +FLT_MIN, -FLT_MIN, -FLT_MIN };
}

//
//
//

static spn_result_t
spn_pbi_begin(struct spn_path_builder_impl * const impl)
{
  // acquire head block
  spn_pbi_acquire_head(impl);

  return SPN_SUCCESS;
}

//
//
//

STATIC_ASSERT_MACRO_1(SPN_TAGGED_BLOCK_ID_INVALID == UINT32_MAX);

static spn_result_t
spn_pbi_end(struct spn_path_builder_impl * const impl, spn_path_t * const path)
{
  // finalize all incomplete active subgroups -- note that we don't
  // care about unused remaining subblocks in a block
  spn_pb_finalize_subgroups(impl);

  // mark remaining ids in the head or node as invalid
  memset(impl->wip.node, 0xFF, sizeof(*impl->wip.node) * impl->wip.rem);

  // get the head dispatch
  struct spn_pbi_dispatch * const dispatch = spn_pbi_dispatch_head(impl);

  // acquire path host id
  spn_device_handle_pool_acquire(impl->device, &path->handle);

  // register handle with wip dispatch
  spn_device_dispatch_register_handle(impl->device, dispatch->id, path->handle);

  // update wip dispatch record
  spn_pbi_dispatch_append(impl, dispatch, path);

  // save path host handle
  impl->wip.header.named.handle = path->handle;

  uint32_t const   offset = impl->wip.head.idx * impl->config.block_dwords;
  uint32_t * const head   = impl->mapped.blocks.u32 + offset;

  // copy header to mapped coherent head block
  memcpy(head, impl->wip.header.array, sizeof(impl->wip.header));

  // reset wip
  spn_pbi_wip_reset(impl);

  // eagerly flush?
  if (dispatch->blocks.span >= impl->config.eager_size)
    {
      spn_pbi_flush(impl);
    }

  return SPN_SUCCESS;
}

//
//
//

static spn_result_t
spn_pbi_release(struct spn_path_builder_impl * const impl)
{
  //
  // launch any wip dispatch
  //
  spn_pbi_flush(impl);

  //
  // wait for all in-flight dispatches to complete
  //
  struct spn_ring * const   ring   = &impl->dispatches.ring;
  struct spn_device * const device = impl->device;

  while (!spn_ring_is_full(ring))
    {
      spn(device_wait(device, __func__));
    }

  //
  // Note that we don't have to unmap before freeing
  //

  //
  // free device allocations
  //
  spn_allocator_device_perm_free(&device->allocator.device.perm.hw_dr,
                                 &device->environment,
                                 &impl->vk.ring.dbi,
                                 impl->vk.ring.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.drw,
                                 &device->environment,
                                 &impl->vk.alloc.dbi,
                                 impl->vk.alloc.dm);

  // free host allocations
  struct spn_allocator_host_perm * const perm = &impl->device->allocator.host.perm;

  spn_allocator_host_perm_free(perm, impl->dispatches.extent);
  spn_allocator_host_perm_free(perm, impl->paths.extent);

  spn_allocator_host_perm_free(perm, impl->path_builder);
  spn_allocator_host_perm_free(perm, impl);

  return SPN_SUCCESS;
}

//
//
//

spn_result_t
spn_path_builder_impl_create(struct spn_device * const        device,
                             struct spn_path_builder ** const path_builder)
{
  //
  // retain the context
  // spn_context_retain(context);
  //
  struct spn_allocator_host_perm * const perm = &device->allocator.host.perm;

  //
  // allocate impl
  //
  struct spn_path_builder_impl * const impl =
    spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, sizeof(*impl));

  //
  // allocate path builder
  //
  struct spn_path_builder * const pb =
    spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, sizeof(*pb));

  // init impl and pb back-pointers
  *path_builder      = pb;
  impl->path_builder = pb;
  pb->impl           = impl;

  // save device
  impl->device = device;

  // get target config
  struct spn_vk_target_config const * const config = spn_vk_get_config(device->instance);

  // stash device-specific params
  uint32_t const block_dwords    = 1u << config->block_pool.block_dwords_log2;
  uint32_t const subblock_dwords = 1u << config->block_pool.subblock_dwords_log2;
  uint32_t const subgroup_dwords = 1u << config->p.group_sizes.named.paths_copy.subgroup_log2;

  impl->config.block_dwords       = block_dwords;
  impl->config.block_subgroups    = block_dwords / subgroup_dwords;
  impl->config.subgroup_dwords    = subgroup_dwords;
  impl->config.subgroup_subblocks = subgroup_dwords / subblock_dwords;

  impl->config.rolling_one = (block_dwords / subblock_dwords) << SPN_TAGGED_BLOCK_ID_BITS_TAG;
  impl->config.eager_size  = config->path_builder.size.eager;

  uint32_t const max_in_flight = config->path_builder.size.dispatches;

  SPN_VK_TRACE_PATH_BUILDER_CREATE(impl, max_in_flight);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.drw,
                                  &device->environment,
                                  sizeof(uint32_t) * max_in_flight,
                                  NULL,
                                  &impl->vk.alloc.dbi,
                                  &impl->vk.alloc.dm);

  uint32_t const ring_size = config->path_builder.size.ring;

  //
  // initialize mapped counters
  //
  spn_ring_init(&impl->mapped.ring, ring_size);

  impl->mapped.rolling = 0;

  // each ring entry is a block of dwords and a one dword cmd
  uint32_t const extent_dwords = ring_size * (block_dwords + 1);
  size_t const   extent_size   = extent_dwords * sizeof(uint32_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.hw_dr,
                                  &device->environment,
                                  extent_size,
                                  NULL,
                                  &impl->vk.ring.dbi,
                                  &impl->vk.ring.dm);

  // map and initialize blocks and cmds
  vk(MapMemory(device->environment.d,
               impl->vk.ring.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.blocks.u32));

  uint32_t const cmds_offset = ring_size * block_dwords;

  impl->mapped.cmds = impl->mapped.blocks.u32 + cmds_offset;

  //
  // allocate path release extent
  //
  size_t const paths_size = sizeof(*impl->paths.extent) * ring_size;

  impl->paths.extent = spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, paths_size);

  spn_next_init(&impl->paths.next, ring_size);

  //
  // reset wip after mapped counters and path release extent
  //
  spn_pbi_wip_reset(impl);

  //
  // allocate dispatches ring
  //
  size_t const dispatches_size = sizeof(*impl->dispatches.extent) * max_in_flight;

  impl->dispatches.extent = spn_allocator_host_perm_alloc(perm,  //
                                                          SPN_MEM_FLAGS_READ_WRITE,
                                                          dispatches_size);

  spn_ring_init(&impl->dispatches.ring, max_in_flight);

  spn_pbi_dispatch_init(impl, &impl->dispatches.ring);

  //
  // init path builder pfns and rem count
  //
  pb->begin   = spn_pbi_begin;
  pb->end     = spn_pbi_end;
  pb->release = spn_pbi_release;
  pb->flush   = spn_pbi_flush;

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n) pb->_p = SPN_PBI_PFN_NAME(_p);

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()

  //
  // init refcount & state
  //
  pb->refcount = 1;

  SPN_ASSERT_STATE_INIT(SPN_PATH_BUILDER_STATE_READY, pb);

  return SPN_SUCCESS;
}

//
//
//
