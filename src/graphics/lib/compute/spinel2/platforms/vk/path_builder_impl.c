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
#include "core_c.h"
#include "device.h"
#include "handle_pool.h"
#include "queue_pool.h"
#include "ring.h"
#include "shaders/push.h"
#include "spinel/spinel_assert.h"

//
// Verify the path header size matches core.h
//
STATIC_ASSERT_MACRO_1(sizeof(union spinel_path_header) == SPN_PATH_HEAD_DWORDS * sizeof(uint32_t));

//
// The path builder moves bulk path data, nodes and a single header from the
// host into the device-managed "block" memory pool.  The data is arranged into
// a SIMT/SIMD-friendly data structure that can be efficiently read by the
// rasterizer.
//
// A simplifying assumption is that the maximum length of a single path can't be
// larger than what fits in the path builder ring.
//
// If a path is too long then the path builder instance is lost.
//
// Note that this restriction can be removed with added complexity to the
// builder and shader.
//
// Also note that for some systems, it may be appropriate to never pull path
// data into the device-managed block pool and instead present the path data to
// the device in a temporarily available allocated memory "zone" of paths that
// can be discarded all at once.
//
// For other systems, it may be appropriate to simply copy the path data from
// host to device.
//
// The general strategy that this particular Vulkan implementation uses is to
// allocate a large "HOST_COHERENT" bulk-data path buffer and an auxiliary
// mappable command buffer.
//
// The work-in-progress path's header and latest node are updated locally until
// full and then stored because the mapped HOST_COHERENT memory is likely
// uncached and read-modify-writes will be expensive.
//
// A line/quad/cubic/rat_quad/rat_cubic acquires 4/6/8/7/10 segments which may
// be spread across one or more contiguous blocks.
//
// If a flush() occurs, then the remaining columns of multi-segment paths are
// initialized with zero-length path primitives.
//
// Every block's command word has a type and a count acquired from a rolling
// counter.
//
// Note that the maximum number of "in-flight" path copy grids is conveniently
// determined by the size of the fence pool.
//

//
// A dispatch record represents a contiguous region of the ring that can be
// copied to or read from the device.
//
// There should be enough dispatch records available so that if they're all in
// flight then either a PCIe or memory bandwidth "roofline" limit is reached.
//
// The expectation is that the path builder will *not* be CPU bound.
//
// The number of dispatch records is defined in the target's config data
// structure.
//
struct spinel_pbi_head_span
{
  uint32_t head;
  uint32_t span;
};

struct spinel_pbi_dispatch
{
  struct spinel_pbi_head_span blocks;
  struct spinel_pbi_head_span paths;

  uint32_t rolling;  // FIXME(allanmac): move to wip

  struct
  {
    spinel_deps_delayed_semaphore_t delayed;
  } signal;
};

//
//
//

struct spinel_pbi_vk
{
  struct spinel_dbi_dm_devaddr alloc;
  struct spinel_dbi_dm_devaddr ring;
};

//
//
//

struct spinel_path_builder_impl
{
  struct spinel_path_builder * path_builder;
  struct spinel_device *       device;
  struct spinel_pbi_vk         vk;

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
    struct spinel_ring ring;

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
    union spinel_path_header header;

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
    spinel_handle_t *  extent;
    struct spinel_next next;
  } paths;

  struct
  {
    struct spinel_pbi_dispatch * extent;
    struct spinel_ring           ring;
  } dispatches;
};

//
//
//
static spinel_result_t
spinel_pbi_lost_begin(struct spinel_path_builder_impl * impl)
{
  return SPN_ERROR_PATH_BUILDER_LOST;
}

static spinel_result_t
spinel_pbi_lost_end(struct spinel_path_builder_impl * impl, spinel_path_t * path)
{
  *path = SPN_PATH_INVALID;

  return SPN_ERROR_PATH_BUILDER_LOST;
}

static spinel_result_t
spinel_pbi_release(struct spinel_path_builder_impl * impl);

static spinel_result_t
spinel_pbi_lost_release(struct spinel_path_builder_impl * impl)
{
  //
  // FIXME -- releasing a lost path builder might eventually require a
  // specialized function.  For now, just call the default release.
  //
  return spinel_pbi_release(impl);
}

static spinel_result_t
spinel_pbi_lost_flush(struct spinel_path_builder_impl * impl)
{
  return SPN_ERROR_PATH_BUILDER_LOST;
}

//
// Define primitive geometry "lost" pfns
//
#define SPN_PBI_PFN_LOST_NAME(_p) spinel_pbi_lost_##_p

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n)                                            \
  static spinel_result_t SPN_PBI_PFN_LOST_NAME(_p)(struct spinel_path_builder_impl * impl)         \
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
spinel_pbi_lost(struct spinel_path_builder_impl * impl)
{
  struct spinel_path_builder * pb = impl->path_builder;

  pb->begin   = spinel_pbi_lost_begin;
  pb->end     = spinel_pbi_lost_end;
  pb->release = spinel_pbi_lost_release;
  pb->flush   = spinel_pbi_lost_flush;

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n) pb->_p = SPN_PBI_PFN_LOST_NAME(_p);

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()
}

//
// Append path to path release extent -- note that this resource is
// implicitly "clocked" by the mapped.ring.
//
static void
spinel_pbi_path_append(struct spinel_path_builder_impl * impl, spinel_path_t const * path)
{
  uint32_t const idx = spinel_next_acquire_1(&impl->paths.next);

  impl->paths.extent[idx] = path->handle;
}

//
// A dispatch captures how many paths and blocks are in a dispatched or
// the work-in-progress compute grid.
//
static struct spinel_pbi_dispatch *
spinel_pbi_dispatch_head(struct spinel_path_builder_impl * impl)
{
  assert(!spinel_ring_is_empty(&impl->dispatches.ring));

  return impl->dispatches.extent + impl->dispatches.ring.head;
}

static struct spinel_pbi_dispatch *
spinel_pbi_dispatch_tail(struct spinel_path_builder_impl * impl)
{
  assert(!spinel_ring_is_full(&impl->dispatches.ring));

  return impl->dispatches.extent + impl->dispatches.ring.tail;
}

static void
spinel_pbi_dispatch_head_init(struct spinel_path_builder_impl * impl)
{
  *spinel_pbi_dispatch_head(impl) = (struct spinel_pbi_dispatch){
    .blocks  = { .head = impl->wip.head.idx,     //
                 .span = 0 },                    //
    .paths   = { .head = impl->paths.next.head,  //
                 .span = 0 },                    //
    .rolling = impl->wip.head.rolling,           //
    .signal  = { .delayed = SPN_DEPS_DELAYED_SEMAPHORE_INVALID },
  };
}

static void
spinel_pbi_dispatch_drop(struct spinel_path_builder_impl * impl)
{
  struct spinel_ring * const ring = &impl->dispatches.ring;

  spinel_ring_drop_1(ring);
}

static void
spinel_pbi_dispatch_acquire(struct spinel_path_builder_impl * impl)
{
  struct spinel_ring * const   ring   = &impl->dispatches.ring;
  struct spinel_device * const device = impl->device;

  while (spinel_ring_is_empty(ring))
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  spinel_pbi_dispatch_head_init(impl);
}

static void
spinel_pbi_dispatch_append(struct spinel_path_builder_impl * impl,
                           struct spinel_pbi_dispatch *      dispatch,
                           spinel_path_t const *             path)
{
  spinel_pbi_path_append(impl, path);

  // clang-format off
  dispatch->blocks.span += impl->wip.header.named.blocks;
  dispatch->paths.span  += 1;
  // clang-format on
}

//
//
//
static void
spinel_pbi_flush_complete(void * data0, void * data1)
{
  struct spinel_path_builder_impl * const impl     = data0;
  struct spinel_pbi_dispatch * const      dispatch = data1;
  struct spinel_device * const            device   = impl->device;

  //
  // These path handles are now materialized
  //
  spinel_deps_delayed_detach_ring(device->deps,
                                  impl->paths.extent,
                                  impl->paths.next.size,
                                  dispatch->paths.head,
                                  dispatch->paths.span);

  //
  // Release the paths -- may invoke wait()
  //
  spinel_device_release_d_paths_ring(device,
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
  dispatch->signal.delayed = SPN_DEPS_DELAYED_SEMAPHORE_INVALID;

  struct spinel_pbi_dispatch * tail = spinel_pbi_dispatch_tail(impl);

  while (tail->signal.delayed == SPN_DEPS_DELAYED_SEMAPHORE_INVALID)
    {
      // release the blocks and cmds
      spinel_ring_release_n(&impl->mapped.ring, tail->blocks.span);

      // release the dispatch
      spinel_ring_release_n(&impl->dispatches.ring, 1);

      // any dispatches in flight?
      if (spinel_ring_is_full(&impl->dispatches.ring))
        {
          break;
        }

      // get new tail
      tail = spinel_pbi_dispatch_tail(impl);
    }
}

//
//
//
static VkPipelineStageFlags
spinel_pbi_flush_record(VkCommandBuffer cb, void * data0, void * data1)
{
  struct spinel_path_builder_impl * const impl     = data0;
  struct spinel_pbi_dispatch * const      dispatch = data1;
  struct spinel_device * const            device   = impl->device;

  ////////////////////////////////////////////////////////////////
  //
  // PATHS ALLOC
  //
  ////////////////////////////////////////////////////////////////

  struct spinel_push_paths_alloc const push_paths_alloc = {

    .devaddr_block_pool_ids   = device->block_pool.vk.dbi_devaddr.ids.devaddr,
    .devaddr_paths_copy_alloc = impl->vk.alloc.devaddr,
    .pc_alloc_idx             = impl->dispatches.ring.head,
    .pc_span                  = dispatch->blocks.span
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.paths_alloc,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_paths_alloc),
                     &push_paths_alloc);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.paths_alloc);

  vkCmdDispatch(cb, 1, 1, 1);

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER: COMPUTE>COMPUTE
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PATHS COPY
  //
  ////////////////////////////////////////////////////////////////

  struct spinel_push_paths_copy const push_paths_copy = {

    .devaddr_block_pool_ids      = device->block_pool.vk.dbi_devaddr.ids.devaddr,
    .devaddr_block_pool_blocks   = device->block_pool.vk.dbi_devaddr.blocks.devaddr,
    .devaddr_block_pool_host_map = device->block_pool.vk.dbi_devaddr.host_map.devaddr,
    .devaddr_paths_copy_alloc    = impl->vk.alloc.devaddr,
    .devaddr_paths_copy_ring     = impl->vk.ring.devaddr,
    .bp_mask                     = device->block_pool.bp_mask,
    .pc_alloc_idx                = impl->dispatches.ring.head,
    .pc_span                     = dispatch->blocks.span,
    .pc_head                     = dispatch->blocks.head,
    .pc_rolling                  = dispatch->rolling,
    .pc_size                     = impl->mapped.ring.size,
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.paths_copy,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_paths_copy),
                     &push_paths_copy);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.paths_copy);

  //
  // Dispatch one subgroup per block
  //
  struct spinel_target_config const * const config = &device->ti.config;

  uint32_t const sgs_per_wg = config->group_sizes.named.paths_copy.workgroup >>
                              config->group_sizes.named.paths_copy.subgroup_log2;

  uint32_t const wg_count = (dispatch->blocks.span + sgs_per_wg - 1) / sgs_per_wg;

  vkCmdDispatch(cb, wg_count, 1, 1);

  //
  // NOTE(allanmac):
  //
  // The `deps` scheduler assumes that the command buffers associated with
  // delayed semaphores always end with a with a compute shader
  // (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT).
  //
  // Only the path builder and raster builder acquire delayes semaphores.
  //
  return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
}

//
//
//
static void
spinel_pbi_flush_submit(void * data0, void * data1)
{
  struct spinel_path_builder_impl * const impl     = data0;
  struct spinel_pbi_dispatch * const      dispatch = data1;

  assert(dispatch->paths.span > 0);

  //
  // Acquire an immediate semaphore
  //
  // Doesn't wait on any handles.
  //
  struct spinel_deps_immediate_submit_info const disi = {
    .record = {
      .pfn   = spinel_pbi_flush_record,
      .data0 = impl,
      .data1 = dispatch,
    },
    //
    // Path builder has no delayed handle dependency
    //
    .completion = {
      .pfn   = spinel_pbi_flush_complete,
      .data0 = impl,
      .data1 = dispatch,
    },
    .signal = {
      .delayed = {
        .count      = 1,
        .semaphores = {
          dispatch->signal.delayed,
        },
      },
    },
  };

  //
  // The current dispatch is now sealed so drop it
  //
  spinel_pbi_dispatch_drop(impl);

  //
  // We don't need to save the returned immediate semaphore.
  //
  struct spinel_device * const device = impl->device;

  spinel_deps_immediate_submit(device->deps, &device->vk, &disi, NULL);

  //
  // Acquire and initialize the next dispatch
  //
  spinel_pbi_dispatch_acquire(impl);
}

//
//
//
static spinel_result_t
spinel_pbi_flush(struct spinel_path_builder_impl * impl)
{
  //
  // Anything to launch?
  //
  struct spinel_pbi_dispatch * const dispatch = spinel_pbi_dispatch_head(impl);

  if (dispatch->paths.span == 0)
    {
      return SPN_SUCCESS;
    }

  //
  // Invoke the delayed submission action
  //
  spinel_deps_delayed_flush(impl->device->deps, dispatch->signal.delayed);

  return SPN_SUCCESS;
}

//
// Before returning a path handle, any remaining coordinates in the
// subgroups(s) are finalized with zero-length primitives.
//
static void
spinel_pb_cn_coords_zero(float * coords, uint32_t rem)
{
  do
    {
      *coords++ = 0.0f;
  } while (--rem > 0);
}

static void
spinel_pb_cn_coords_finalize(float * coords[], uint32_t coords_len, uint32_t const rem)
{
  do
    {
      spinel_pb_cn_coords_zero(*coords++, rem);
  } while (--coords_len > 0);
}

static void
spinel_pb_finalize_subgroups(struct spinel_path_builder_impl * impl)
{
  struct spinel_path_builder * const pb = impl->path_builder;

  //
  // Note that this zeroes a cacheline / subblock at a time
  //
#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n)                                            \
  {                                                                                                \
    uint32_t rem = pb->cn.rem._p;                                                                  \
                                                                                                   \
    if (rem > 0)                                                                                   \
      {                                                                                            \
        pb->cn.rem._p = 0;                                                                         \
                                                                                                   \
        spinel_pb_cn_coords_finalize(pb->cn.coords._p, _n, rem);                                   \
      }                                                                                            \
  }

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()
}

//
//
//
static void
spinel_pbi_cmd_append(struct spinel_path_builder_impl * impl,
                      uint32_t const                    idx,
                      uint32_t const                    type)
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
spinel_pbi_node_append_next(struct spinel_path_builder_impl * impl)
{
  // no need to increment the node pointer
  *impl->wip.node = impl->mapped.rolling | SPN_BLOCK_ID_TAG_PATH_NEXT;
}

//
//
//
static uint32_t
spinel_pbi_acquire_head_block(struct spinel_path_builder_impl * impl)
{
  struct spinel_ring * const ring = &impl->mapped.ring;

  // is ring full?
  if (spinel_ring_is_empty(ring))
    {
      // launch any unlaunched dispatch
      spinel_pbi_flush(impl);

      struct spinel_device * const device = impl->device;

      do
        {
          // wait for at least one dispatch to complete
          spinel_deps_drain_1(device->deps, &device->vk);

      } while (spinel_ring_is_empty(ring));
    }

  return spinel_ring_acquire_1(&impl->mapped.ring);
}

static spinel_result_t
spinel_pbi_acquire_node_segs_block(struct spinel_path_builder_impl * impl, uint32_t * idx)
{
  struct spinel_ring * const ring = &impl->mapped.ring;

  if (spinel_ring_is_empty(ring))
    {
      //
      // If the work in progress is going to exceed the size of the ring
      // then this is a fatal error. At this point, we can kill the path
      // builder instead of the device.
      //
      if (impl->wip.header.named.blocks >= impl->mapped.ring.size)
        {
          spinel_pbi_lost(impl);

          return SPN_ERROR_PATH_BUILDER_LOST;  // FIXME(allanmac): return a "TOO_LONG" error?
        }

      //
      // Otherwise, launch whatever is in the ring...
      //
      spinel_pbi_flush(impl);

      //
      // ... and wait for blocks to appear in the ring!
      //
      struct spinel_device * const device = impl->device;

      do
        {
          // wait for at least one dispatch to complete
          spinel_deps_drain_1(device->deps, &device->vk);

      } while (spinel_ring_is_empty(ring));
    }

  *idx = spinel_ring_acquire_1(&impl->mapped.ring);

  return SPN_SUCCESS;
}

//
//
//
static void
spinel_pbi_acquire_head(struct spinel_path_builder_impl * impl)
{
  uint32_t const idx = spinel_pbi_acquire_head_block(impl);

  spinel_pbi_cmd_append(impl, idx, SPN_PATHS_COPY_CMD_TYPE_HEAD);

  uint32_t const   offset = idx * impl->config.block_dwords;
  uint32_t * const head   = impl->mapped.blocks.u32 + offset;

  impl->wip.node = head + SPN_PATH_HEAD_DWORDS;
  impl->wip.rem  = impl->config.block_dwords - SPN_PATH_HEAD_DWORDS;
}

static spinel_result_t
spinel_pbi_acquire_node(struct spinel_path_builder_impl * impl)
{
  spinel_pbi_node_append_next(impl);

  uint32_t              idx;
  spinel_result_t const err = spinel_pbi_acquire_node_segs_block(impl, &idx);

  if (err != SPN_SUCCESS)
    {
      return err;
    }

  spinel_pbi_cmd_append(impl, idx, SPN_PATHS_COPY_CMD_TYPE_NODE);

  impl->wip.header.named.nodes += 1;

  uint32_t const offset = idx * impl->config.block_dwords;

  impl->wip.node = impl->mapped.blocks.u32 + offset;
  impl->wip.rem  = impl->config.block_dwords;

  return SPN_SUCCESS;
}

static spinel_result_t
spinel_pbi_acquire_segs(struct spinel_path_builder_impl * impl)
{
  uint32_t idx;

  spinel_result_t const err = spinel_pbi_acquire_node_segs_block(impl, &idx);

  if (err != SPN_SUCCESS)
    {
      return err;
    }

  impl->wip.segs.rolling = impl->mapped.rolling;

  spinel_pbi_cmd_append(impl, idx, SPN_PATHS_COPY_CMD_TYPE_SEGS);

  uint32_t const offset = idx * impl->config.block_dwords;

  impl->mapped.subgroups.f32 = impl->mapped.blocks.f32 + offset;
  impl->mapped.subgroups.rem = impl->config.block_subgroups;

  return SPN_SUCCESS;
}

//
//
//
static void
spinel_pbi_node_append_segs(struct spinel_path_builder_impl * impl, uint32_t const tag)
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
static spinel_result_t
spinel_pbi_prim_acquire_subgroups(struct spinel_path_builder_impl * impl,
                                  uint32_t const                    tag,
                                  float **                          coords,
                                  uint32_t                          coords_len)
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
          spinel_result_t const err = spinel_pbi_acquire_node(impl);

          if (err != SPN_SUCCESS)
            return err;
        }

      // are there no subgroups left?
      if (impl->mapped.subgroups.rem == 0)
        {
          spinel_result_t const err = spinel_pbi_acquire_segs(impl);

          if (err != SPN_SUCCESS)
            return err;
        }

      // record the tagged block id
      spinel_pbi_node_append_segs(impl, curr_tag);

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

#define SPN_PBI_PFN_NAME(_p) spinel_pbi_##_p

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n)                                            \
  static spinel_result_t SPN_PBI_PFN_NAME(_p)(struct spinel_path_builder_impl * impl)              \
  {                                                                                                \
    return spinel_pbi_prim_acquire_subgroups(impl, _i, impl->path_builder->cn.coords._p, _n);      \
  }

SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()

//
//
//
STATIC_ASSERT_MACRO_1(sizeof(union spinel_path_header) ==
                      MEMBER_SIZE_MACRO(union spinel_path_header, array));

static void
spinel_pbi_wip_reset(struct spinel_path_builder_impl * impl)
{
  struct spinel_path_builder * const pb = impl->path_builder;

  // init path builder counters
#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n) pb->cn.rem._p = 0;

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND();

  // save mapped head to wip
  impl->wip.head.idx     = impl->mapped.ring.head;
  impl->wip.head.rolling = impl->mapped.rolling;

  // there are no subblocks available
  impl->mapped.subgroups.rem = 0;

  // update header -- don't bother initializing .handle and .na
  impl->wip.header.named.blocks = 0;
  impl->wip.header.named.nodes  = 0;

  // reset prim counters
  memset(impl->wip.header.named.prims.array, 0, sizeof(impl->wip.header.named.prims.array));

  // reset bounds
  impl->wip.header.named.bounds[0] = FLT_MAX;
  impl->wip.header.named.bounds[1] = FLT_MAX;
  impl->wip.header.named.bounds[2] = FLT_MIN;
  impl->wip.header.named.bounds[3] = FLT_MIN;
}

//
//
//
static spinel_result_t
spinel_pbi_begin(struct spinel_path_builder_impl * impl)
{
  // acquire head block
  spinel_pbi_acquire_head(impl);

  return SPN_SUCCESS;
}

//
//
//
STATIC_ASSERT_MACRO_1(SPN_TAGGED_BLOCK_ID_INVALID == UINT32_MAX);

static spinel_result_t
spinel_pbi_end(struct spinel_path_builder_impl * impl, spinel_path_t * path)
{
  // finalize all incomplete active subgroups -- note that we don't
  // care about unused remaining subblocks in a block
  spinel_pb_finalize_subgroups(impl);

  // mark remaining ids in the head or node as invalid
  memset(impl->wip.node, 0xFF, sizeof(*impl->wip.node) * impl->wip.rem);

  // device
  struct spinel_device * const device = impl->device;

  // get the head dispatch
  struct spinel_pbi_dispatch * const dispatch = spinel_pbi_dispatch_head(impl);

  // do we need to acquire a delayed semaphore?
  if (dispatch->signal.delayed == SPN_DEPS_DELAYED_SEMAPHORE_INVALID)
    {
      struct spinel_deps_acquire_delayed_info const dadi = {

        .submission = { .pfn   = spinel_pbi_flush_submit,  //
                        .data0 = impl,
                        .data1 = dispatch }
      };

      dispatch->signal.delayed = spinel_deps_delayed_acquire(device->deps, &device->vk, &dadi);
    }

  // acquire path host id
  path->handle = spinel_device_handle_acquire(device);

  // update device-side path header with host-side path handle
  impl->wip.header.named.handle = path->handle;

  // associate delayed semaphore with handle
  spinel_deps_delayed_attach(device->deps, path->handle, dispatch->signal.delayed);

  // append path to dispatch
  spinel_pbi_dispatch_append(impl, dispatch, path);

  uint32_t const   offset = impl->wip.head.idx * impl->config.block_dwords;
  uint32_t * const head   = impl->mapped.blocks.u32 + offset;

  // copy wip header to mapped coherent head block
  memcpy(head, impl->wip.header.array, sizeof(impl->wip.header));

  // reset wip header
  spinel_pbi_wip_reset(impl);

  // eagerly flush?
  if (dispatch->blocks.span >= impl->config.eager_size)
    {
      spinel_deps_delayed_flush(device->deps, dispatch->signal.delayed);
    }

  return SPN_SUCCESS;
}

//
//
//
static spinel_result_t
spinel_pbi_release(struct spinel_path_builder_impl * impl)
{
  //
  // Launch any wip dispatch
  //
  spinel_pbi_flush(impl);

  //
  // Wait for all in-flight dispatches to complete
  //
  struct spinel_ring * const   ring   = &impl->dispatches.ring;
  struct spinel_device * const device = impl->device;

  while (!spinel_ring_is_full(ring))
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  //
  // Free device allocations.
  //
  // Note that we don't have to unmap before freeing.
  //
  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.hw_dr,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.ring.dbi_dm);

  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.alloc.dbi_dm);

  //
  // Free host allocations
  //
  free(impl->dispatches.extent);
  free(impl->paths.extent);
  free(impl->path_builder);
  free(impl);

  spinel_context_release(device->context);

  return SPN_SUCCESS;
}

//
//
//
spinel_result_t
spinel_path_builder_impl_create(struct spinel_device *        device,
                                struct spinel_path_builder ** path_builder)
{
  spinel_context_retain(device->context);

  //
  // allocate impl
  //
  struct spinel_path_builder_impl * const impl = malloc(sizeof(*impl));

  //
  // allocate path builder
  //
  struct spinel_path_builder * const pb = malloc(sizeof(*pb));

  // init impl and pb back-pointers
  *path_builder      = pb;
  impl->path_builder = pb;
  pb->impl           = impl;

  // save device
  impl->device = device;

  //
  // init path builder pfns and rem count
  //
  pb->begin   = spinel_pbi_begin;
  pb->end     = spinel_pbi_end;
  pb->release = spinel_pbi_release;
  pb->flush   = spinel_pbi_flush;

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n) pb->_p = SPN_PBI_PFN_NAME(_p);

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()

  //
  // init refcount & state
  //
  pb->ref_count = 1;

  SPN_ASSERT_STATE_INIT(SPN_PATH_BUILDER_STATE_READY, pb);

  //
  // get target config
  //
  struct spinel_target_config const * const config = &device->ti.config;

  //
  // FIXME(allanmac): Stop replicating these constants in the impl?
  //
  // stash device-specific params
  uint32_t const block_dwords    = 1u << config->block_pool.block_dwords_log2;
  uint32_t const subblock_dwords = 1u << config->block_pool.subblock_dwords_log2;
  uint32_t const subgroup_dwords = 1u << config->group_sizes.named.paths_copy.subgroup_log2;

  impl->config.block_dwords       = block_dwords;
  impl->config.block_subgroups    = block_dwords / subgroup_dwords;
  impl->config.subgroup_dwords    = subgroup_dwords;
  impl->config.subgroup_subblocks = subgroup_dwords / subblock_dwords;

  impl->config.rolling_one = (block_dwords / subblock_dwords) << SPN_TAGGED_BLOCK_ID_BITS_TAG;
  impl->config.eager_size  = config->path_builder.size.eager;

  uint32_t const max_in_flight = config->path_builder.size.dispatches;

  spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.drw,
                                        device->vk.pd,
                                        device->vk.d,
                                        device->vk.ac,
                                        sizeof(uint32_t) * max_in_flight,
                                        NULL,
                                        &impl->vk.alloc);

  uint32_t const ring_size = config->path_builder.size.ring;

  //
  // initialize mapped counters
  //
  spinel_ring_init(&impl->mapped.ring, ring_size);

  impl->mapped.rolling = 0;

  // each ring entry is a block of dwords and a one dword cmd
  uint32_t const extent_dwords = ring_size * (block_dwords + 1);
  size_t const   extent_size   = extent_dwords * sizeof(uint32_t);

  spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.hw_dr,
                                        device->vk.pd,
                                        device->vk.d,
                                        device->vk.ac,
                                        extent_size,
                                        NULL,
                                        &impl->vk.ring);

  // map and initialize blocks and cmds
  vk(MapMemory(device->vk.d,
               impl->vk.ring.dbi_dm.dm,
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

  impl->paths.extent = malloc(paths_size);

  spinel_next_init(&impl->paths.next, ring_size);

  //
  // reset wip after mapped counters and path release extent
  //
  spinel_pbi_wip_reset(impl);

  //
  // allocate dispatches ring
  //
  size_t const dispatches_size = sizeof(*impl->dispatches.extent) * max_in_flight;

  impl->dispatches.extent = malloc(dispatches_size);

  spinel_ring_init(&impl->dispatches.ring, max_in_flight);

  spinel_pbi_dispatch_head_init(impl);

  return SPN_SUCCESS;
}

//
//
//
