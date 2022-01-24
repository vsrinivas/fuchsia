// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "raster_builder_impl.h"

#include <memory.h>
#include <stdlib.h>

#include "block_pool.h"
#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "handle_pool.h"
#include "path_builder.h"
#include "path_builder_impl.h"
#include "queue_pool.h"
#include "ring.h"
#include "shaders/push.h"
#include "spinel/spinel_assert.h"
#include "weakref.h"

//
// The raster builder prepares fill commands, transforms and clips for the
// rasterization sub-pipeline.
//
// A simplifying assumption is that the maximum length of a single raster can't
// be larger than what fits in the raster builder ring.
//
// This would be a very long raster and is a legitimate size limitation.
//
// If a raster is exceeds this limit then the raster builder instance is lost.
//
// Note that this restriction can be removed with added complexity to the
// builder and shaders.
//
// The general strategy that this particular Vulkan implementation uses is to
// allocate a large "HOST_COHERENT" buffer for the ring.
//
// Note that the maximum number of "in-flight" rasterization sub-pipelines is
// conveniently determined by the size of the fence pool.
//
// The size of ring buffer is driven by the desired size limit of a single
// raster.
//
// The worst-case total storage per fill() invocation is:
//
//   coherent
//     - fills      : 4 dwords
//     - transforms : 8 dwords
//     - clips      : 4 dwords
//   host
//     - paths      : 1 dword
//     - rasters    : 1 dword +
//                   ----------
//                   18 dwords
//
// There are a maximum of (SPN_RASTER_COHORT_METAS_SIZE-1) rasters in a single
// cohort.
//
// A single raster will necessarily have a maximum number of
// paths/transforms/clips.
//
// Exceeding this limit terminates the raster builder.
//
// Note that the fills/paths count will always be 1:1 and potentially greater
// than the varying transforms/clips/rasters counts.
//
// Worst case is that the fills/transforms/clips/paths/rasters counts are all
// equal.
//
// Note that fill commands, transforms and clips may be read more than once by
// the rasterization sub-pipeline.
//
// Depending on the device architecture, it may be beneficial to copy the
// working region of the coherent buffer to a device-local buffer.
//
// If the Vulkan device is integrated or supports mapped write-through (AMD)
// then we don't need to copy.  If the device is discrete and doesn't support
// write-through (NVIDIA) then we do.
//
// Note that the fill command can reduce its transform and clip fields to 13-16
// bits and fit into 3 dwords but it's easier to use a uint4 with GPUs.
//
// A non-affine transformation elevates a Bezier to a rational.  For this
// reason, we indicate with a bit flag if the transform matrix has non-zero
// {w0,w1} elements.
//

//
//
// clang-format off
#define SPN_RASTER_BUILDER_RAST_TYPE_EXPAND()                                     \
  SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X(proj_line,  SPN_RAST_TYPE_PROJ_LINE,  4)  \
  SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X(proj_quad,  SPN_RAST_TYPE_PROJ_QUAD,  6)  \
  SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X(proj_cubic, SPN_RAST_TYPE_PROJ_CUBIC, 8)  \
  SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X(line,       SPN_RAST_TYPE_LINE,       4)  \
  SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X(quad,       SPN_RAST_TYPE_QUAD,       6)  \
  SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X(cubic,      SPN_RAST_TYPE_CUBIC,      8)  \
  SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X(rat_quad,   SPN_RAST_TYPE_RAT_QUAD,   7)  \
  SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X(rat_cubic,  SPN_RAST_TYPE_RAT_CUBIC, 10)

//
// TODO(allanmac): Unify the .cf and .tc rings since they're both quads.
//

//
// The fill command layout is the same on both the host and device.
//
struct spinel_cmd_fill
{
  uint32_t path_h;              // host id
  uint32_t na0            : 16; // unused
  uint32_t cohort         : 15; // cohort is 8-11 bits
  uint32_t transform_type : 1;  // transform type: 0=affine,1=projective
  uint32_t transform;           // index of first quad of transform
  uint32_t clip;                // index of clip quad
};
// clang-format on

STATIC_ASSERT_MACRO_1(sizeof(struct spinel_cmd_fill) == sizeof(uint32_t[4]));

//
// Ring work span
//
struct spinel_rbi_head_span
{
  uint32_t head;
  uint32_t span;
};

//
// Dispatch states
//
typedef enum spinel_rbi_dispatch_state_e
{
  SPN_RBI_DISPATCH_STATE_INVALID,
  SPN_RBI_DISPATCH_STATE_RECORDING,
  SPN_RBI_DISPATCH_STATE_PENDING,
  SPN_RBI_DISPATCH_STATE_COMPLETE,

} spinel_rbi_dispatch_state_e;

//
// There are always as many dispatch records as there are fences in
// the fence pool.  This simplifies reasoning about concurrency.
//
// FIXME(allanmac): We don't have to track tc/rc once submitted
// FIXME(allanmac): We probably can drop most of the dbi structs
//
struct spinel_rbi_dispatch
{
  struct
  {
    struct
    {
      struct spinel_dbi_devaddr internal;
      struct spinel_dbi_devaddr indirect;
    } rs;

    struct spinel_dbi_devaddr ttrks;             // ttrks + ttrks_keyvals_even
    struct spinel_dbi_devaddr fill_scan;         // used before sorting
    struct spinel_dbi_devaddr rast_cmds;         // used before sorting
    struct spinel_dbi_devaddr ttrk_keyvals_odd;  // used by radix and post-sort
  } vk;

  struct spinel_rbi_head_span cf;  // fills and paths are 1:1
  struct spinel_rbi_head_span tc;  // transform quads and clips
  struct spinel_rbi_head_span rc;  // rasters in cohort

  spinel_deps_delayed_semaphore_t delayed;

  spinel_rbi_dispatch_state_e state;
};

//
// The host-side rings share a single host-coherent buffer:
//
//   |<--cmds(uvec4)-->|<--transform.lo/hi & clip(vec4)-->|<--raster_h(uint)-->|
//
// Each ring has a different access pattern:
//
//      ring    | reads
//   -----------+-------
//   cmd_fills  |   2
//   transforms |   1+
//   clips      |   1+
//   rasters    |   1
//
// For this reason, some Vulkan devices may benefit from copying the
// ring spans from the host-coherent buffer to a device-local buffer.
//
struct spinel_rbi_vk
{
  struct
  {
    struct
    {
      struct spinel_dbi_dm_devaddr h;
      struct spinel_dbi_dm_devaddr d;
    } cf;

    struct
    {
      struct spinel_dbi_dm_devaddr h;
      struct spinel_dbi_dm_devaddr d;
    } tc;

    struct
    {
      struct spinel_dbi_dm_devaddr h;
      struct spinel_dbi_dm_devaddr d;
    } rc;
  } rings;

  struct
  {
    struct spinel_dbi_dm ttrks;
    struct spinel_dbi_dm rfs_rrc_tko;

    struct
    {
      struct spinel_dbi_dm internal;
      struct spinel_dbi_dm indirect;
    } rs;
  } dispatch;
};

//
//
//
struct spinel_raster_builder_impl
{
  struct spinel_raster_builder * raster_builder;
  struct spinel_device *         device;
  struct spinel_rbi_vk           vk;

  //
  // As noted above, the remaining slots in the fills ring is always
  // greater-than-or-equal to the remaining slots in the tcs ring so
  // we use simpler accounting for tcs and rc.
  //
  struct
  {
    struct
    {
      struct spinel_cmd_fill * extent;
      struct spinel_ring       ring;
    } cf;  // fill commands

    struct
    {
      SPN_TYPE_F32VEC4 * extent;
      struct spinel_next next;
    } tc;  // transforms & clips

    struct
    {
      spinel_handle_t *  extent;
      struct spinel_next next;
    } rc;  // rasters in cohort
  } mapped;

  //
  // work in progress raster
  //
  struct
  {
    struct spinel_rbi_head_span cf;  // fill commands
    struct spinel_rbi_head_span tc;  // transforms and clips
  } wip;

  //
  // Resources released upon an grid completion:
  //
  //   - Path handles can be released after rasterization stage.
  //
  //   - Raster handles can be released after the entire rasterization
  //     sub-pipeline completes.
  //
  //   - Dispatch records and associated mapped spans released in
  //     ring order.
  //
  spinel_weakref_epoch_t epoch;

  struct
  {
    spinel_handle_t * extent;
  } paths;

  struct
  {
    spinel_handle_t * extent;
  } rasters;

  struct
  {
    struct spinel_rbi_dispatch * extent;
    struct spinel_ring           ring;
  } dispatches;
};

//
//
//
static bool
spinel_rbi_is_staged(struct spinel_target_config const * config)
{
  return ((config->allocator.device.hw_dr.properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0);
}

//
// These pfns are installed when the Spinel context is lost
//
static spinel_result_t
spinel_rbi_lost_begin(struct spinel_raster_builder_impl * impl)
{
  return SPN_ERROR_RASTER_BUILDER_LOST;
}

static spinel_result_t
spinel_rbi_lost_end(struct spinel_raster_builder_impl * impl, spinel_raster_t * raster)
{
  *raster = SPN_RASTER_INVALID;  // FIXME -- SPN_TYPED_HANDLE_INVALID

  return SPN_ERROR_RASTER_BUILDER_LOST;
}

static spinel_result_t
spinel_rbi_release(struct spinel_raster_builder_impl * impl);

static spinel_result_t
spinel_rbi_lost_release(struct spinel_raster_builder_impl * impl)
{
  //
  // FIXME -- releasing a lost path builder might eventually require a
  // specialized function.  For now, just call the default release.
  //
  return spinel_rbi_release(impl);
}

static spinel_result_t
spinel_rbi_lost_flush(struct spinel_raster_builder_impl * impl)
{
  return SPN_ERROR_RASTER_BUILDER_LOST;
}

static spinel_result_t
spinel_rbi_lost_add(struct spinel_raster_builder_impl * impl,
                    spinel_path_t const *               paths,
                    spinel_transform_weakref_t *        transform_weakrefs,
                    spinel_transform_t const *          transforms,
                    spinel_clip_weakref_t *             clip_weakrefs,
                    spinel_clip_t const *               clips,
                    uint32_t                            count)
{
  return SPN_ERROR_RASTER_BUILDER_LOST;
}

//
// If (wip.span == mapped.ring.size) then the raster is too long and
// the raster builder is terminally "lost".  The raster builder should
// be released and a new one created.
//
static void
spinel_rbi_lost(struct spinel_raster_builder_impl * impl)
{
  struct spinel_raster_builder * const rb = impl->raster_builder;

  rb->begin   = spinel_rbi_lost_begin;
  rb->end     = spinel_rbi_lost_end;
  rb->release = spinel_rbi_lost_release;
  rb->flush   = spinel_rbi_lost_flush;
  rb->add     = spinel_rbi_lost_add;
}

static void
spinel_rbi_raster_append(struct spinel_raster_builder_impl * impl, spinel_handle_t handle)
{
  uint32_t const idx = spinel_next_acquire_1(&impl->mapped.rc.next);

  impl->mapped.rc.extent[idx] = handle;  // device
  impl->rasters.extent[idx]   = handle;  // host
}

//
// A dispatch captures how many paths and blocks are in a dispatched or
// the work-in-progress compute grid.
//
static struct spinel_rbi_dispatch *
spinel_rbi_dispatch_head(struct spinel_raster_builder_impl * impl)
{
  assert(!spinel_ring_is_empty(&impl->dispatches.ring));

  return impl->dispatches.extent + impl->dispatches.ring.head;
}

static struct spinel_rbi_dispatch *
spinel_rbi_dispatch_tail(struct spinel_raster_builder_impl * impl)
{
  assert(!spinel_ring_is_full(&impl->dispatches.ring));

  return impl->dispatches.extent + impl->dispatches.ring.tail;
}

static void
spinel_rbi_dispatch_drop(struct spinel_raster_builder_impl * impl)
{
  struct spinel_ring * const ring = &impl->dispatches.ring;

  spinel_ring_drop_1(ring);
}

static void
spinel_rbi_dispatch_head_init(struct spinel_raster_builder_impl * impl)
{
  //
  // Don't initialize this with a spinel_rbi_dispatch struct and designated
  // initializers because each dispatch structure has precalculated .dbi
  // buffers.
  //
  // Per-member initializers are fine.
  //
  struct spinel_rbi_dispatch * const dispatch = spinel_rbi_dispatch_head(impl);

  assert(dispatch->state == SPN_RBI_DISPATCH_STATE_INVALID);

  dispatch->cf = impl->wip.cf;
  dispatch->tc = impl->wip.tc;
  dispatch->rc = (struct spinel_rbi_head_span){ .head = impl->mapped.rc.next.head, .span = 0 };

  dispatch->delayed = SPN_DEPS_DELAYED_SEMAPHORE_INVALID;
  dispatch->state   = SPN_RBI_DISPATCH_STATE_RECORDING;
}

static void
spinel_rbi_dispatch_acquire(struct spinel_raster_builder_impl * impl)
{
  struct spinel_ring * const   ring   = &impl->dispatches.ring;
  struct spinel_device * const device = impl->device;

  while (spinel_ring_is_empty(ring))
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  spinel_rbi_dispatch_head_init(impl);
}

//
//
//
static void
spinel_rbi_dispatch_append_wip(struct spinel_raster_builder_impl * impl,
                               struct spinel_rbi_dispatch *        dispatch)
{
  dispatch->cf.span += impl->wip.cf.span;
  dispatch->tc.span += impl->wip.tc.span;
  dispatch->rc.span += 1;
}

//
// We record where the *next* work-in-progress raster will start in the ring
// along with its rolling counter.
//
static void
spinel_rbi_wip_reset(struct spinel_raster_builder_impl * impl)
{
  impl->wip.cf = (struct spinel_rbi_head_span){ .head = impl->mapped.cf.ring.head, .span = 0 };
  impl->wip.tc = (struct spinel_rbi_head_span){ .head = impl->mapped.tc.next.head, .span = 0 };
}

//
//
//
static void
spinel_rbi_copy_ring(VkCommandBuffer                     cb,
                     VkDescriptorBufferInfo const *      h,
                     VkDescriptorBufferInfo const *      d,
                     VkDeviceSize                        elem_size,
                     uint32_t                            ring_size,
                     struct spinel_rbi_head_span const * head_span)
{
  VkBufferCopy bcs[2];
  uint32_t     bc_count;

  bool const     is_wrap   = (head_span->span + head_span->head) > ring_size;
  uint32_t const span_hi   = is_wrap ? (ring_size - head_span->head) : head_span->span;
  VkDeviceSize   offset_hi = elem_size * head_span->head;

  bcs[0].srcOffset = h->offset + offset_hi;
  bcs[0].dstOffset = d->offset + offset_hi;
  bcs[0].size      = elem_size * span_hi;

  if (is_wrap)
    {
      uint32_t const span_lo = head_span->span - span_hi;

      bcs[1].srcOffset = h->offset;
      bcs[1].dstOffset = d->offset;
      bcs[1].size      = elem_size * span_lo;

      bc_count = 2;
    }
  else
    {
      bc_count = 1;
    }

  vkCmdCopyBuffer(cb, h->buffer, d->buffer, bc_count, bcs);
}

//
//
//
static void
spinel_rbi_flush_complete(void * data0, void * data1)
{
  struct spinel_raster_builder_impl * const impl     = data0;
  struct spinel_rbi_dispatch * const        dispatch = data1;
  struct spinel_device * const              device   = impl->device;

  //
  // These raster handles are now materialized so invalidate their dependencies.
  //
  spinel_deps_delayed_detach_ring(device->deps,
                                  impl->rasters.extent,
                                  impl->mapped.rc.next.size,
                                  dispatch->rc.head,
                                  dispatch->rc.span);

  //
  // Release paths -- may invoke wait()
  //
  // FIXME(allanmac): Paths could be released much earlier if we're willing to
  // complicate the submission and launch an additional command buffer.
  //
  spinel_device_release_d_paths_ring(device,
                                     impl->paths.extent,
                                     impl->mapped.cf.ring.size,
                                     dispatch->cf.head,
                                     dispatch->cf.span);
  //
  // Release the rasters -- may invoke wait()
  //
  spinel_device_release_d_rasters_ring(device,
                                       impl->rasters.extent,
                                       impl->mapped.rc.next.size,
                                       dispatch->rc.head,
                                       dispatch->rc.span);
  //
  // If the dispatch is the tail of the ring then try to release as many
  // dispatch records as possible...
  //
  // Note that dispatches can complete in any order but the ring releases need
  // to occur in order.
  //
  assert(dispatch->state != SPN_RBI_DISPATCH_STATE_COMPLETE);

  dispatch->state = SPN_RBI_DISPATCH_STATE_COMPLETE;

  struct spinel_rbi_dispatch * tail = spinel_rbi_dispatch_tail(impl);

  while (tail->state == SPN_RBI_DISPATCH_STATE_COMPLETE)
    {
      // will always be true
      assert(impl->mapped.cf.ring.tail == tail->cf.head);

      // release the blocks and cmds
      spinel_ring_release_n(&impl->mapped.cf.ring, tail->cf.span);

      // release the dispatch
      spinel_ring_release_n(&impl->dispatches.ring, 1);

      // mark as invalid
      tail->state = SPN_RBI_DISPATCH_STATE_INVALID;

      // any dispatches still in flight?
      if (spinel_ring_is_full(&impl->dispatches.ring))
        {
          break;
        }

      // get new tail
      tail = spinel_rbi_dispatch_tail(impl);
    }
}

//
//
//
static VkPipelineStageFlags
spinel_rbi_flush_record(VkCommandBuffer cb, void * data0, void * data1)
{
  //
  // 0. ZEROES & COPY
  //
  //    Prepares device-side data structures.
  //
  // 1. FILL_SCAN
  //
  //    Compute the prefix sum of each path type in the fill's path.
  //
  // 2. FILL_DISPATCH
  //
  //    Take the atomically updated count of rasterization commands and
  //    initialize a workgroup triple for vkCmdDispatchIndirect().
  //
  // 3. FILL_EXPAND
  //
  //    Expand the fill command into rasterization commands and store them to
  //    a temporary buffer:
  //
  //      |<lines><quads><cubics><rat_quads><rat_cubics>|
  //
  // 4. RASTERIZE_LINES/QUADS/CUBICS/RAT_QUADS/RAT_CUBICS
  //
  //    For each path type, indirectly dispatch a rasterizer.
  //
  // 5. INDIRECT RADIX SORT TTRK KEYS
  //
  // 6. SEGMENT_TTRK_DISPATCH
  //
  // 7. SEGMENT_TTRK
  //
  // 8. RASTERS_ALLOC
  //
  // 9. RASTERS_PREFIX
  //
  struct spinel_raster_builder_impl * const impl     = data0;
  struct spinel_rbi_dispatch * const        dispatch = data1;
  struct spinel_device * const              device   = impl->device;

  ////////////////////////////////////////////////////////////////
  //
  // FILL: ZERO RASTER COHORT META TABLE
  //
  ////////////////////////////////////////////////////////////////

  {
    //
    // Zero ttrks SoA arrays *after* .alloc[]
    //
    // NOTE(allanmac): This fill has no dependencies until step (7) so it can be
    // delayed.
    //
    VkDeviceSize const offset = SPN_BUFFER_OFFSETOF(ttrks, meta.rk_off);
    VkDeviceSize const size   = SPN_BUFFER_MEMBER_SIZE(ttrks, meta) - offset;

    vkCmdFillBuffer(cb,
                    dispatch->vk.ttrks.dbi.buffer,
                    dispatch->vk.ttrks.dbi.offset + offset,
                    size,
                    0);
  }

  ////////////////////////////////////////////////////////////////
  //
  // FILL: ZERO TTRKS.COUNT_DISPATCH
  //
  // FIXME(allanmac): This fill can be combined with the above zeroing fill.
  //
  ////////////////////////////////////////////////////////////////

  {
    VkDeviceSize const offset = SPN_BUFFER_OFFSETOF(ttrks, count_dispatch);
    VkDeviceSize const size   = SPN_BUFFER_MEMBER_SIZE(ttrks, count_dispatch);

    vkCmdFillBuffer(cb,
                    dispatch->vk.ttrks.dbi.buffer,
                    dispatch->vk.ttrks.dbi.offset + offset,
                    size,
                    0);
  }

  ////////////////////////////////////////////////////////////////
  //
  // FILL: ZERO RASTERIZE.FILL_SCAN_COUNTS
  //
  ////////////////////////////////////////////////////////////////

  {
    VkDeviceSize const offset = SPN_BUFFER_OFFSETOF(rasterize_fill_scan, counts);
    VkDeviceSize const size   = SPN_BUFFER_MEMBER_SIZE(rasterize_fill_scan, counts);

    vkCmdFillBuffer(cb,
                    dispatch->vk.fill_scan.dbi.buffer,
                    dispatch->vk.fill_scan.dbi.offset + offset,
                    size,
                    0);
  }

  ////////////////////////////////////////////////////////////////
  //
  // COPY COMMAND RINGS
  //
  // On a discrete GPU, 1-2 regions of 3 rings are copied from H>D
  //
  // FIXME(allanmac): Only the .cf ring is used by fill_scan so the .tc and .rc
  // copies could be delayed.
  //
  ////////////////////////////////////////////////////////////////

  struct spinel_target_config const * const config = &device->ti.config;

  if (spinel_rbi_is_staged(config))
    {
      // CF
      spinel_rbi_copy_ring(cb,
                           &impl->vk.rings.cf.h.dbi_dm.dbi,
                           &impl->vk.rings.cf.d.dbi_dm.dbi,
                           sizeof(*impl->mapped.cf.extent),
                           impl->mapped.cf.ring.size,
                           &dispatch->cf);

      // TC
      spinel_rbi_copy_ring(cb,
                           &impl->vk.rings.tc.h.dbi_dm.dbi,
                           &impl->vk.rings.tc.d.dbi_dm.dbi,
                           sizeof(*impl->mapped.tc.extent),
                           impl->mapped.tc.next.size,
                           &dispatch->tc);

      // RC
      spinel_rbi_copy_ring(cb,
                           &impl->vk.rings.rc.h.dbi_dm.dbi,
                           &impl->vk.rings.rc.d.dbi_dm.dbi,
                           sizeof(*impl->mapped.rc.extent),
                           impl->mapped.rc.next.size,
                           &dispatch->rc);
    }

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER: FILLS & COPIES
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_transfer_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: FILL_SCAN
  //
  ////////////////////////////////////////////////////////////////

  struct spinel_push_fill_scan const push_fill_scan = {

    .devaddr_rasterize_fill_scan = dispatch->vk.fill_scan.devaddr,
    .devaddr_rasterize_fill_cmds = impl->vk.rings.cf.d.devaddr,
    .devaddr_block_pool_blocks   = device->block_pool.vk.dbi_devaddr.blocks.devaddr,
    .devaddr_block_pool_host_map = device->block_pool.vk.dbi_devaddr.host_map.devaddr,
    .cmd_head                    = dispatch->cf.head,
    .cmd_size                    = impl->mapped.cf.ring.size,
    .cmd_span                    = dispatch->cf.span
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.fill_scan,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_fill_scan),
                     &push_fill_scan);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.fill_scan);

  {
    //
    // Each invocation processes multiple commands.
    //
    uint32_t const cmds_per_wg = config->raster_builder.fill_scan.rows *  //
                                 config->group_sizes.named.fill_scan.workgroup;

    uint32_t const wg_count = (dispatch->cf.span + cmds_per_wg - 1) / cmds_per_wg;

    vkCmdDispatch(cb, wg_count, 1, 1);
  }

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER: COMPUTE>COMPUTE
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: FILL_DISPATCH
  //
  // NOTE: PUSH CONSTANTS ARE COMPATIBLE WITH FILL_SCAN
  //
  // A single workgroup initializes the indirect dispatches
  //
  // Either 4 or 8 invocations are required (SPN_RAST_TYPE_COUNT == 8)
  //
  ////////////////////////////////////////////////////////////////

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.fill_dispatch);

  vkCmdDispatch(cb, 1, 1, 1);

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER: COMPUTE>COMPUTE
  //
  // Note that FILL_EXPAND reads the u32vec4 initialized by FILL_DISPATCH but
  // only RASTERIZE_XXX indirectly dispatches off of the u32vec4.
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: FILL_EXPAND
  //
  // NOTE: PUSH CONSTANTS ARE MOSTLY COMPATIBLE WITH FILL_SCAN
  //
  ////////////////////////////////////////////////////////////////

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.fill_expand,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     SPN_PUSH_OFFSETOF(fill_expand, devaddr_rasterize_rast_cmds),
                     SPN_PUSH_MEMBER_SIZE(fill_expand, devaddr_rasterize_rast_cmds),
                     &dispatch->vk.rast_cmds.devaddr);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.fill_expand);

  {
    //
    // Dispatch one subgroup per command
    //
    uint32_t const sgs_per_wg = config->group_sizes.named.fill_expand.workgroup >>  //
                                config->group_sizes.named.fill_expand.subgroup_log2;

    uint32_t const wg_count = (dispatch->cf.span + sgs_per_wg - 1) / sgs_per_wg;

    vkCmdDispatch(cb, wg_count, 1, 1);
  }

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER: COMPUTE>INDIRECT|COMPUTE
  //
  // Note that the indirect dispatch was initialized by `fill_dispatch`.
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_indirect_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // TODO(allanmac): PIPELINE RASTERIZE_DISPATCH
  //
  // The indirect dispatch of the rasterization *may* need to support workgroups
  // larger than one subgroup if the device architecture doesn't achieve max
  // residency when dispatching single subgroup workgroups.  This would require
  // another compute shader to adjust the dispatch counts.  For now, just assume
  // (workgroup_size == subgroup_size).
  //
  ////////////////////////////////////////////////////////////////

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINES: RASTERIZE_PROJ_LINE
  //            RASTERIZE_PROJ_QUAD
  //            RASTERIZE_PROJ_CUBIC
  //            RASTERIZE_LINE
  //            RASTERIZE_QUAD
  //            RASTERIZE_CUBIC
  //            RASTERIZE_RAT_QUAD
  //            RASTERIZE_RAT_CUBIC
  //
  ////////////////////////////////////////////////////////////////

  assert(config->group_sizes.named.rasterize_line.workgroup ==
         (1u << config->group_sizes.named.rasterize_line.subgroup_log2));

  struct spinel_push_rasterize const push_rasterize = {

    .devaddr_block_pool_ids       = device->block_pool.vk.dbi_devaddr.ids.devaddr,
    .devaddr_block_pool_blocks    = device->block_pool.vk.dbi_devaddr.blocks.devaddr,
    .devaddr_rasterize_fill_quads = impl->vk.rings.tc.d.devaddr,
    .devaddr_rasterize_fill_scan  = dispatch->vk.fill_scan.devaddr,
    .devaddr_rasterize_rast_cmds  = dispatch->vk.rast_cmds.devaddr,
    .devaddr_ttrks                = dispatch->vk.ttrks.devaddr,
    .bp_mask                      = device->block_pool.bp_mask,
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.rasterize_line,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_rasterize),
                     &push_rasterize);

  VkBuffer const     rasterize_buffer = dispatch->vk.fill_scan.dbi.buffer;
  VkDeviceSize const rasterize_offset = dispatch->vk.fill_scan.dbi.offset +  //
                                        SPN_BUFFER_OFFSETOF(rasterize_fill_scan, dispatch);

#undef SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X
#define SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X(_p, _i, _n)                                          \
  {                                                                                                \
    vkCmdBindPipeline(cb,                                                                          \
                      VK_PIPELINE_BIND_POINT_COMPUTE,                                              \
                      device->ti.pipelines.named.rasterize_##_p);                                  \
                                                                                                   \
    vkCmdDispatchIndirect(cb, rasterize_buffer, rasterize_offset + _i * sizeof(SPN_TYPE_U32VEC4)); \
  }

  SPN_RASTER_BUILDER_RAST_TYPE_EXPAND()

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER: COMPUTE>INDIRECT|COMPUTE
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_indirect_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // RADIX SORT INDIRECT
  //
  // The "rasterize_fill_scan" "rasterize_rast_cmds" extents are no longer used
  // at this point.
  //
  ////////////////////////////////////////////////////////////////

  VkDescriptorBufferInfo const dbi_ttrks_count = {

    .buffer = dispatch->vk.ttrks.dbi.buffer,
    .offset = dispatch->vk.ttrks.dbi.offset + SPN_BUFFER_OFFSETOF(ttrks, count_dispatch.w),
    .range  = sizeof(uint32_t)
  };

  VkDescriptorBufferInfo const dbi_ttrk_keyvals_even = {

    .buffer = dispatch->vk.ttrks.dbi.buffer,
    .offset = dispatch->vk.ttrks.dbi.offset + SPN_BUFFER_OFFSETOF(ttrks, keyvals),
    .range  = dispatch->vk.ttrks.dbi.range - SPN_BUFFER_OFFSETOF(ttrks, keyvals)
  };

  struct radix_sort_vk_sort_indirect_info const info = {

    .ext          = NULL,
    .key_bits     = SPN_TTRK_BITS_XY_COHORT,
    .count        = &dbi_ttrks_count,
    .keyvals_even = &dbi_ttrk_keyvals_even,
    .keyvals_odd  = &dispatch->vk.ttrk_keyvals_odd.dbi,
    .internal     = &dispatch->vk.rs.internal.dbi,
    .indirect     = &dispatch->vk.rs.indirect.dbi
  };

  VkDescriptorBufferInfo dbi_ttrk_keyvals_out;

  radix_sort_vk_sort_indirect(device->ti.rs, &info, device->vk.d, cb, &dbi_ttrk_keyvals_out);

  // Device address of extent output by radix sort
  VkDeviceAddress const devaddr_ttrk_keyvals_out = spinel_dbi_to_devaddr(device->vk.d,  //
                                                                         &dbi_ttrk_keyvals_out);

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER: COMPUTE>COMPUTE
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: TTRKS_SEGMENT_DISPATCH
  //
  // FIXME(allanmac): push_ttrks_segment_dispatch is "push compatible" with
  // push_ttrks_segment
  //
  ////////////////////////////////////////////////////////////////

  struct spinel_push_ttrks_segment_dispatch const push_ttrks_segment_dispatch = {
    .devaddr_ttrks_header = dispatch->vk.ttrks.devaddr,
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.ttrks_segment_dispatch,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_ttrks_segment_dispatch),
                     &push_ttrks_segment_dispatch);

  vkCmdBindPipeline(cb,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    device->ti.pipelines.named.ttrks_segment_dispatch);

  vkCmdDispatch(cb, 1, 1, 1);  // A single invocation initializes the indirect dispatches

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER: COMPUTE>INDIRECT|COMPUTE
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_indirect_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: TTRKS_SEGMENT
  //
  // FIXME(allanmac): push_ttrks_segment_dispatch is "push compatible" with
  // push_ttrks_segment
  //
  ////////////////////////////////////////////////////////////////

  struct spinel_push_ttrks_segment const push_ttrks_segment = {
    .devaddr_ttrks_header = dispatch->vk.ttrks.devaddr,
    .devaddr_ttrk_keyvals = devaddr_ttrk_keyvals_out,
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.ttrks_segment,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_ttrks_segment),
                     &push_ttrks_segment);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.ttrks_segment);

  VkDeviceSize const ttrks_segment_count_dispatch_offset =
    dispatch->vk.ttrks.dbi.offset + SPN_BUFFER_OFFSETOF(ttrks, count_dispatch);

  vkCmdDispatchIndirect(cb, dispatch->vk.ttrks.dbi.buffer, ttrks_segment_count_dispatch_offset);

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER: COMPUTE>COMPUTE
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: RASTERS_ALLOC
  //
  ////////////////////////////////////////////////////////////////

  struct spinel_push_rasters_alloc const push_rasters_alloc = {
    .devaddr_raster_ids          = impl->vk.rings.rc.d.devaddr,
    .devaddr_ttrks_header        = dispatch->vk.ttrks.devaddr,
    .devaddr_block_pool_ids      = device->block_pool.vk.dbi_devaddr.ids.devaddr,
    .devaddr_block_pool_blocks   = device->block_pool.vk.dbi_devaddr.blocks.devaddr,
    .devaddr_block_pool_host_map = device->block_pool.vk.dbi_devaddr.host_map.devaddr,
    .ids_size                    = impl->mapped.rc.next.size,
    .ids_head                    = dispatch->rc.head,
    .ids_span                    = dispatch->rc.span,
    .bp_mask                     = device->block_pool.bp_mask,
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.rasters_alloc,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_rasters_alloc),
                     &push_rasters_alloc);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.rasters_alloc);

  {
    //
    // Dispatch one thread per raster rounded up to a workgroup
    //
    uint32_t const wg_size  = config->group_sizes.named.rasters_alloc.workgroup;
    uint32_t const wg_count = (dispatch->rc.span + wg_size - 1) / wg_size;

    vkCmdDispatch(cb, wg_count, 1, 1);
  }

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER: COMPUTE>COMPUTE
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: RASTERS_PREFIX
  //
  ////////////////////////////////////////////////////////////////

  struct spinel_push_rasters_prefix const push_rasters_prefix = {
    .devaddr_block_pool_ids    = device->block_pool.vk.dbi_devaddr.ids.devaddr,
    .devaddr_block_pool_blocks = device->block_pool.vk.dbi_devaddr.blocks.devaddr,
    .devaddr_ttrks_header      = dispatch->vk.ttrks.devaddr,
    .devaddr_ttrk_keyvals      = devaddr_ttrk_keyvals_out,
    .ids_size                  = impl->mapped.rc.next.size,
    .ids_head                  = dispatch->rc.head,
    .ids_span                  = dispatch->rc.span,
    .bp_mask                   = device->block_pool.bp_mask,
  };

  vkCmdPushConstants(cb,
                     device->ti.pipeline_layouts.named.rasters_prefix,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_rasters_prefix),
                     &push_rasters_prefix);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->ti.pipelines.named.rasters_prefix);

  {
    //
    // Dispatch one subgroup per raster
    //
    uint32_t const sgs_per_wg = config->group_sizes.named.rasters_prefix.workgroup >>
                                config->group_sizes.named.rasters_prefix.subgroup_log2;

    uint32_t const wg_count = (dispatch->rc.span + sgs_per_wg - 1) / sgs_per_wg;

    vkCmdDispatch(cb, wg_count, 1, 1);
  }

  //
  // NOTE(allanmac):
  //
  // The `deps` scheduler assumes that the command buffers associated with
  // delayed semaphores always end with a with a compute shader
  // (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT).
  //
  // Only the path builder and raster builder acquire delayed semaphores.
  //
  return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
}

//
//
//
static void
spinel_rbi_flush_submit(void * data0, void * data1)
{
  struct spinel_raster_builder_impl * const impl     = data0;
  struct spinel_rbi_dispatch * const        dispatch = data1;

  assert(dispatch->cf.span > 0);
  assert(dispatch == spinel_rbi_dispatch_head(impl));
  assert(dispatch->state == SPN_RBI_DISPATCH_STATE_RECORDING);

  //
  // Acquire an immediate semaphore
  //
  struct spinel_deps_immediate_submit_info const disi = {
    .record = {
      .pfn   = spinel_rbi_flush_record,
      .data0 = impl,
      .data1 = dispatch,
    },
    .wait = {
      .delayed = {
        .handles = {
          .extent = impl->paths.extent,
          .size   = impl->mapped.cf.ring.size,
          .head   = dispatch->cf.head,
          .span   = dispatch->cf.span,
        },
      },
    },
    .completion = {
      .pfn   = spinel_rbi_flush_complete,
      .data0 = impl,
      .data1 = dispatch,
    },
    .signal = {
      .delayed = {
        .count      = 1,
        .semaphores = {
          dispatch->delayed,
        },
      },
    },
  };

  //
  // Invalidate all outstanding transform and clip weakrefs
  //
  spinel_weakref_epoch_increment(&impl->epoch);

  //
  // The current dispatch is in flight so drop it
  //
  spinel_rbi_dispatch_drop(impl);

  //
  // Move to pending state
  //
  dispatch->state = SPN_RBI_DISPATCH_STATE_PENDING;

  //
  // We don't need to save the immediate semaphore
  //
  struct spinel_device * const device = impl->device;

  spinel_deps_immediate_submit(device->deps, &device->vk, &disi, NULL);

  //
  // Acquire and initialize the next dispatch
  //
  spinel_rbi_dispatch_acquire(impl);
}

//
//
//
static spinel_result_t
spinel_rbi_flush(struct spinel_raster_builder_impl * impl)
{
  //
  // Anything to launch?
  //
  struct spinel_rbi_dispatch const * const dispatch = spinel_rbi_dispatch_head(impl);

  assert(dispatch->state == SPN_RBI_DISPATCH_STATE_RECORDING);

  if (dispatch->rc.span == 0)
    {
      assert(dispatch->cf.span == 0);

      return SPN_SUCCESS;
    }

  //
  // Invoke the delayed submission action
  //
  spinel_deps_delayed_flush(impl->device->deps, dispatch->delayed);

  return SPN_SUCCESS;
}

//
//
//
static spinel_result_t
spinel_rbi_begin(struct spinel_raster_builder_impl * impl)
{
  return SPN_SUCCESS;
}

//
//
//
static spinel_result_t
spinel_rbi_end(struct spinel_raster_builder_impl * impl, spinel_raster_t * raster)
{
  //
  // acquire raster host id
  //
  struct spinel_device * const device = impl->device;

  raster->handle = spinel_device_handle_acquire(device);

  //
  // get the head dispatch
  //
  struct spinel_rbi_dispatch * const dispatch = spinel_rbi_dispatch_head(impl);

  assert(dispatch->state == SPN_RBI_DISPATCH_STATE_RECORDING);

  //
  // do we need to acquire a delayed semaphore for the dispatch?
  //
  if (dispatch->rc.span == 0)
    {
      struct spinel_deps_acquire_delayed_info const dadi = {
        .submission = { .pfn   = spinel_rbi_flush_submit,  //
                        .data0 = impl,
                        .data1 = dispatch },
      };

      dispatch->delayed = spinel_deps_delayed_acquire(device->deps, &device->vk, &dadi);
    }

  // associate delayed semaphore with handle
  spinel_deps_delayed_attach(device->deps, raster->handle, dispatch->delayed);

  // save raster handle to ring
  spinel_rbi_raster_append(impl, raster->handle);

  // update head dispatch's span
  spinel_rbi_dispatch_append_wip(impl, dispatch);

  // reset wip
  spinel_rbi_wip_reset(impl);

  //
  // * flush if the raster cohort size limit has been reached
  // * flush if the fill command "eager" limit has been reached
  //
  struct spinel_target_config const * const config = &device->ti.config;

  if ((dispatch->rc.span >= config->raster_builder.size.cohort) ||  // "is_rc_full"
      (dispatch->cf.span >= config->raster_builder.size.eager))     // "is_cf_eager"
    {
      return spinel_rbi_flush(impl);
    }

  return SPN_SUCCESS;
}

//
// If the raster builder is directly exposed as a public API then
// validate the transform and clip weakref indices.
//
// If a fuzzer alters the weakref epoch then the weakref is invalid --
// we don't need to check for this case since that's the purpose of
// the weakref.
//
// If a fuzzer alters the weakref's index but its epoch still matches
// the the current epoch then we simply need to validate that its
// index is *potentially* valid -- the weakref might still be
// invalidated by about-to-happen spinel_rbi_flush().
//
// clang-format off
static spinel_result_t
spinel_rbi_validate_transform_weakref_indices(struct spinel_ring const *         cf_ring,
                                              struct spinel_rbi_dispatch const * dispatch,
                                              spinel_transform_weakref_t const * transform_weakrefs,
                                              uint32_t const                           count)
// clang-format on
{
  //
  // FIXME(allanmac)
  //
  // For non-null weakrefs, check to see index is within WIP span
  //
  return SPN_SUCCESS;
}

static spinel_result_t
spinel_rbi_validate_clip_weakref_indices(struct spinel_ring const *         cf_ring,
                                         struct spinel_rbi_dispatch const * dispatch,
                                         spinel_clip_weakref_t const *      clip_weakrefs,
                                         uint32_t const                     count)
{
  //
  // FIXME(allanmac)
  //
  // For non-null weakrefs, check to see index is within WIP span
  //
  return SPN_SUCCESS;
}

//
// Permute lo and hi transform
//
// src: { sx shx tx  shy sy ty w0 w1 } // Row-ordered matrix
// dst: { sx shx shy sy  tx ty w0 w1 } // GPU-friendly ordering
//
static void
spinel_rbi_transform_copy_lo(SPN_TYPE_F32VEC4 * dst, spinel_transform_t const * src)
{
  dst->x = src->sx;
  dst->y = src->shx;
  dst->z = src->shy;
  dst->w = src->sy;
}

static void
spinel_rbi_transform_copy_hi(SPN_TYPE_F32VEC4 * dst, spinel_transform_t const * src)
{
  dst->x = src->tx;
  dst->y = src->ty;
  dst->z = src->w0;
  dst->w = src->w1;
}

//
//
//
static spinel_result_t
spinel_rbi_add(struct spinel_raster_builder_impl * impl,
               spinel_path_t const *               paths,
               spinel_transform_weakref_t *        transform_weakrefs,
               spinel_transform_t const *          transforms,
               spinel_clip_weakref_t *             clip_weakrefs,
               spinel_clip_t const *               clips,
               uint32_t                            count)
{
  // Anything to do?
  if (count == 0)
    {
      return SPN_SUCCESS;
    }

  //
  // If the number of work-in-progress paths is larger than the ring then fail
  // hard and lose the raster builder.
  //
  struct spinel_ring * const cf_ring = &impl->mapped.cf.ring;

  if (impl->wip.cf.span + count > cf_ring->size)
    {
      spinel_rbi_lost(impl);

      return SPN_ERROR_RASTER_BUILDER_LOST;
    }

  //
  // If not enough entries are left in the command ring then flush now and wait
  // for cf slots to be made available.
  //
  struct spinel_device * const device = impl->device;

  if (count > cf_ring->rem)
    {
      //
      // Launch whatever is in the ring and then wait for cf slots...
      //
      spinel_rbi_flush(impl);

      // clang-format off
      do {

        spinel_deps_drain_1(device->deps, &device->vk);

      } while (count > cf_ring->rem);
      // clang-format on
    }

  //
  // Validate the paths before we proceed
  //
  spinel_result_t result;

  result = spinel_device_validate_d_paths(device, paths, count);

  if (result != SPN_SUCCESS)
    {
      return result;
    }

  //
  // Validate the transform and clip weakref indices -- this is cheap!
  //
  struct spinel_rbi_dispatch * const dispatch = spinel_rbi_dispatch_head(impl);

  assert(dispatch->state == SPN_RBI_DISPATCH_STATE_RECORDING);

  result = spinel_rbi_validate_transform_weakref_indices(cf_ring,  //
                                                         dispatch,
                                                         transform_weakrefs,
                                                         count);
  if (result != SPN_SUCCESS)
    {
      return result;
    }

  result = spinel_rbi_validate_clip_weakref_indices(cf_ring,  //
                                                    dispatch,
                                                    clip_weakrefs,
                                                    count);

  if (result != SPN_SUCCESS)
    {
      return result;
    }

  //
  // Everything validates... retain the paths on the device
  //
  spinel_device_retain_d_paths(device, paths, count);

  //
  // Increment the cf span
  //
  impl->wip.cf.span += count;

  //
  // There will always be enough room in the TC ring so only its head
  // needs to be tracked.
  //
  struct spinel_next * const tc_next = &impl->mapped.tc.next;

  //
  // The command's cohort id is the same for all commands
  //
  struct spinel_cmd_fill cf = { 0 };

  cf.cohort = dispatch->rc.span;

  //
  // Append commands to the cf ring and dependent quads to the tc ring
  //
  while (true)
    {
      uint32_t const cf_idx = spinel_ring_acquire_1(cf_ring);

      //
      // Get the path
      //
      uint32_t const handle = paths->handle;

      impl->paths.extent[cf_idx] = handle;
      cf.path_h                  = handle;

      //
      // Classify the transform
      //
      // if (w0==w1==0) then it's an affine matrix
      //
      cf.transform_type = ((transforms->w0 == 0.0f) && (transforms->w1 == 0.0f))
                            ? SPN_CMD_FILL_TRANSFORM_TYPE_AFFINE
                            : SPN_CMD_FILL_TRANSFORM_TYPE_PROJECTIVE;

      //
      // If the weakref exists then reuse existing transform index
      //
      if (!spinel_transform_weakrefs_get_index(transform_weakrefs, 0, &impl->epoch, &cf.transform))
        {
          uint32_t       span;
          uint32_t const t_idx = spinel_next_acquire_2(tc_next, &span);

          impl->wip.tc.span += span;

          spinel_transform_weakrefs_init(transform_weakrefs, 0, &impl->epoch, t_idx);

          spinel_rbi_transform_copy_lo(impl->mapped.tc.extent + t_idx + 0, transforms);
          spinel_rbi_transform_copy_hi(impl->mapped.tc.extent + t_idx + 1, transforms);

          cf.transform = t_idx;
        }

      //
      // If the weakref exists then reuse existing clip index
      //
      if (!spinel_clip_weakrefs_get_index(clip_weakrefs, 0, &impl->epoch, &cf.clip))
        {
          uint32_t const c_idx = spinel_next_acquire_1(tc_next);

          impl->wip.tc.span += 1;

          spinel_clip_weakrefs_init(clip_weakrefs, 0, &impl->epoch, c_idx);

          memcpy(impl->mapped.tc.extent + c_idx, clips, sizeof(*impl->mapped.tc.extent));

          cf.clip = c_idx;
        }

      //
      // Store the command to the ring
      //
      impl->mapped.cf.extent[cf_idx] = cf;

      //
      // No more paths?
      //
      if (--count == 0)
        {
          break;
        }

      //
      // Otherwise, increment pointers
      //
      // FIXME(allanmac): This will be updated with an argument "template"
      // struct.
      //
      paths += 1;

      if (transform_weakrefs != NULL)
        {
          transform_weakrefs += 1;
        }

      transforms += 1;

      if (clip_weakrefs != NULL)
        {
          clip_weakrefs += 1;
        }

      clips += 1;
    }

  return SPN_SUCCESS;
}

//
//
//
static spinel_result_t
spinel_rbi_release(struct spinel_raster_builder_impl * impl)
{
  //
  // launch any undispatched rasters
  //
  spinel_rbi_flush(impl);

  //
  // wait for all in-flight dispatches to complete
  //
  struct spinel_ring * const   ring   = &impl->dispatches.ring;
  struct spinel_device * const device = impl->device;

  while (!spinel_ring_is_full(ring))
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  //
  // Dispatch extents
  //
  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.dispatch.rfs_rrc_tko);

  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.dispatch.ttrks);

  //
  // Radix Sort extents
  //
  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.dispatch.rs.indirect);

  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.dispatch.rs.internal);

  //
  // Ring staging extents
  //
  struct spinel_target_config const * const config = &device->ti.config;

  if (spinel_rbi_is_staged(config))
    {
      spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                                   device->vk.d,
                                   device->vk.ac,
                                   &impl->vk.rings.rc.d.dbi_dm);

      spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                                   device->vk.d,
                                   device->vk.ac,
                                   &impl->vk.rings.tc.d.dbi_dm);

      spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                                   device->vk.d,
                                   device->vk.ac,
                                   &impl->vk.rings.cf.d.dbi_dm);
    }

  //
  // Ring extents
  //
  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.hw_dr,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.rings.rc.h.dbi_dm);

  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.hw_dr,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.rings.tc.h.dbi_dm);

  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.hw_dr,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.rings.cf.h.dbi_dm);
  //
  // Free host allocations
  //
  free(impl->rasters.extent);
  free(impl->paths.extent);
  free(impl->dispatches.extent);
  free(impl->raster_builder);
  free(impl);

  spinel_context_release(device->context);

  return SPN_SUCCESS;
}

//
//
//
spinel_result_t
spinel_raster_builder_impl_create(struct spinel_device *    device,
                                  spinel_raster_builder_t * raster_builder)
{
  spinel_context_retain(device->context);

  //
  // allocate impl
  //
  struct spinel_raster_builder_impl * const impl = malloc(sizeof(*impl));

  //
  // allocate raster builder
  //
  struct spinel_raster_builder * const rb = malloc(sizeof(*rb));

  // init impl and rb back-pointers
  *raster_builder      = rb;
  impl->raster_builder = rb;
  rb->impl             = impl;

  // save device
  impl->device = device;

  //
  // init raster builder pfns
  //
  rb->begin   = spinel_rbi_begin;
  rb->end     = spinel_rbi_end;
  rb->release = spinel_rbi_release;
  rb->flush   = spinel_rbi_flush;
  rb->add     = spinel_rbi_add;

  //
  // init refcount & state
  //
  rb->ref_count = 1;

  SPN_ASSERT_STATE_INIT(SPN_RASTER_BUILDER_STATE_READY, rb);

  //
  // Allocate rings
  //
  struct spinel_target_config const * const config = &device->ti.config;

  assert(config->raster_builder.size.eager <= config->raster_builder.size.ring);

  //
  // CF: 1 ring entry per command
  //
  uint32_t const cf_ring_size = config->raster_builder.size.ring;

  spinel_ring_init(&impl->mapped.cf.ring, cf_ring_size);

  //
  // TC: 1 transform + 1 clip = 3 quads
  //
  // Worst case is 3 quads per command.
  //
  // NOTE(allanmac): One additional quad is required because transforms require
  // 2 consecutive quads and the worst case would be a full ring of commands
  // each with a transform and clip.
  //
  uint32_t const tc_ring_size = cf_ring_size * 3 + 1;

  spinel_next_init(&impl->mapped.tc.next, tc_ring_size);

  //
  // How many dispatches?
  //
  uint32_t const max_in_flight = config->raster_builder.size.dispatches;

  //
  // RC: Worst case is (cohort size * dispatches) rasters.
  //
  assert(config->raster_builder.size.cohort <= SPN_RASTER_COHORT_MAX_SIZE);

  uint32_t const rc_ring_size = config->raster_builder.size.cohort * max_in_flight;

  spinel_next_init(&impl->mapped.rc.next, rc_ring_size);

  //
  // FIXME(allanmac): Allocate one buffer for all rings and one buffer for all
  // staging buffers.
  //

  //
  // Allocate and map CF
  //
  VkDeviceSize const cf_size = sizeof(*impl->mapped.cf.extent) * cf_ring_size;

  spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.hw_dr,
                                        device->vk.pd,
                                        device->vk.d,
                                        device->vk.ac,
                                        cf_size,
                                        NULL,
                                        &impl->vk.rings.cf.h);

  vk(MapMemory(device->vk.d,
               impl->vk.rings.cf.h.dbi_dm.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.cf.extent));

  //
  // Allocate and map TC
  //
  VkDeviceSize const tc_size = sizeof(*impl->mapped.tc.extent) * tc_ring_size;

  spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.hw_dr,
                                        device->vk.pd,
                                        device->vk.d,
                                        device->vk.ac,
                                        tc_size,
                                        NULL,
                                        &impl->vk.rings.tc.h);

  vk(MapMemory(device->vk.d,
               impl->vk.rings.tc.h.dbi_dm.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.tc.extent));

  //
  // Allocate and map RC
  //
  VkDeviceSize const rc_size = sizeof(*impl->mapped.rc.extent) * rc_ring_size;

  spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.hw_dr,
                                        device->vk.pd,
                                        device->vk.d,
                                        device->vk.ac,
                                        rc_size,
                                        NULL,
                                        &impl->vk.rings.rc.h);

  vk(MapMemory(device->vk.d,
               impl->vk.rings.rc.h.dbi_dm.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.rc.extent));

  //
  // Are host-writable rings used as staging buffers?
  //
  if (spinel_rbi_is_staged(config))
    {
      spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.drw,
                                            device->vk.pd,
                                            device->vk.d,
                                            device->vk.ac,
                                            cf_size,
                                            NULL,
                                            &impl->vk.rings.cf.d);

      spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.drw,
                                            device->vk.pd,
                                            device->vk.d,
                                            device->vk.ac,
                                            tc_size,
                                            NULL,
                                            &impl->vk.rings.tc.d);

      spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.drw,
                                            device->vk.pd,
                                            device->vk.d,
                                            device->vk.ac,
                                            rc_size,
                                            NULL,
                                            &impl->vk.rings.rc.d);
    }
  else
    {
      impl->vk.rings.cf.d = impl->vk.rings.cf.h;
      impl->vk.rings.tc.d = impl->vk.rings.tc.h;
      impl->vk.rings.rc.d = impl->vk.rings.rc.h;
    }

  //
  // Allocate dispatches and path/raster release extents
  //
  // Implicitly sets state to SPN_RBI_DISPATCH_STATE_INVALID.
  //
  impl->dispatches.extent = calloc(max_in_flight, sizeof(*impl->dispatches.extent));

  //
  // Allocate paths
  //
  size_t const paths_size = sizeof(*impl->paths.extent) * config->raster_builder.size.ring;

  impl->paths.extent = malloc(paths_size);

  //
  // Allocate rasters
  //
  impl->rasters.extent = malloc(rc_size);  // same size as mapped size

  //
  // Get radix sort memory requirements
  //
  struct radix_sort_vk_memory_requirements rs_mr;

  radix_sort_vk_get_memory_requirements(device->ti.rs, config->raster_builder.size.ttrks, &rs_mr);

  assert(SPN_MEMBER_ALIGN_LIMIT >= rs_mr.keyvals_alignment);
  assert(SPN_MEMBER_ALIGN_LIMIT >= rs_mr.internal_alignment);
  assert(SPN_MEMBER_ALIGN_LIMIT >= rs_mr.indirect_alignment);

  //
  // What is rounded-up size of ttrks buffer?
  //
  VkDeviceSize const ttrks_size    = SPN_BUFFER_OFFSETOF(ttrks, keyvals) + rs_mr.keyvals_size;
  VkDeviceSize const ttrks_size_ru = ROUND_UP_POW2_MACRO(ttrks_size, SPN_MEMBER_ALIGN_LIMIT);

  //
  // Allocate memory shared across dispatches:
  //
  //   ttrks = max_in_flight * sizeof(ttrks)
  //
  spinel_allocator_alloc_dbi_dm(&device->allocator.device.perm.drw,
                                device->vk.pd,
                                device->vk.d,
                                device->vk.ac,
                                max_in_flight * ttrks_size_ru,
                                NULL,
                                &impl->vk.dispatch.ttrks);
  //
  // Assign bufrefs to dispatches
  //
  // FIXME(allanmac): Do all VK objects need both their .dbis and .devaddrs
  // initialized?
  //
  for (uint32_t ii = 0; ii < max_in_flight; ii++)
    {
      struct spinel_rbi_dispatch * const dispatch = impl->dispatches.extent + ii;

      // vk.ttrks
      spinel_dbi_devaddr_from_dbi(device->vk.d,
                                  &dispatch->vk.ttrks,
                                  &impl->vk.dispatch.ttrks.dbi,
                                  ii * ttrks_size_ru,
                                  ttrks_size_ru);
    }

  //
  // Allocate per-dispatch radix sort internal and indirect buffers
  //
  //   internal = max_in_flight * rs_mr.internal_size
  //   indirect = max_in_flight * rs_mr.indirect_size
  //
  // clang-format off
  VkDeviceSize const rs_internal_size_ru = ROUND_UP_POW2_MACRO(rs_mr.internal_size, SPN_MEMBER_ALIGN_LIMIT);
  VkDeviceSize const rs_indirect_size_ru = ROUND_UP_POW2_MACRO(rs_mr.indirect_size, SPN_MEMBER_ALIGN_LIMIT);
  // clang-format on

  spinel_allocator_alloc_dbi_dm(&device->allocator.device.perm.drw,
                                device->vk.pd,
                                device->vk.d,
                                device->vk.ac,
                                max_in_flight * rs_internal_size_ru,
                                NULL,
                                &impl->vk.dispatch.rs.internal);

  spinel_allocator_alloc_dbi_dm(&device->allocator.device.perm.drw,
                                device->vk.pd,
                                device->vk.d,
                                device->vk.ac,
                                max_in_flight * rs_indirect_size_ru,
                                NULL,
                                &impl->vk.dispatch.rs.indirect);

  //
  // Assign bufrefs to dispatches
  //
  // FIXME(allanmac): Do all VK objects need both their .dbis and .devaddrs
  // initialized?
  //
  for (uint32_t ii = 0; ii < max_in_flight; ii++)
    {
      struct spinel_rbi_dispatch * const dispatch = impl->dispatches.extent + ii;

      // vk.rs.internal
      spinel_dbi_devaddr_from_dbi(device->vk.d,
                                  &dispatch->vk.rs.internal,
                                  &impl->vk.dispatch.rs.internal.dbi,
                                  ii * rs_internal_size_ru,
                                  rs_internal_size_ru);

      // vk.rs.indirect
      spinel_dbi_devaddr_from_dbi(device->vk.d,
                                  &dispatch->vk.rs.indirect,
                                  &impl->vk.dispatch.rs.indirect.dbi,
                                  ii * rs_indirect_size_ru,
                                  rs_indirect_size_ru);
    }

  //
  // More per-dispatch buffers:
  //
  //   rfs: rasterize fill scan
  //   rrc: rasterize rast cmds
  //   tko: ttrk keys odd
  //
  // We conservatively round them up to ensure the buffers are properly aligned
  // on the device.
  //
  // The rfs/rrc extents can be safely aliased by the later-used tko extent.
  //
  // TODO(allanmac): The rounding and aliasing can be refined.
  //
  // clang-format off
  uint32_t     const rfs_sg_size         = 1u << config->group_sizes.named.fill_scan.subgroup_log2;
  VkDeviceSize const rfs_block_size_pow2 = sizeof(SPN_TYPE_U32VEC4) * 2 * rfs_sg_size;
  VkDeviceSize const rfs_prefix_size     = config->raster_builder.size.ring * sizeof(SPN_TYPE_U32VEC4) * 2;
  VkDeviceSize const rfs_prefix_size_ru  = ROUND_UP_POW2_MACRO(rfs_prefix_size, rfs_block_size_pow2);
  VkDeviceSize const rfs_size            = SPN_BUFFER_OFFSETOF(rasterize_fill_scan, prefix) + rfs_prefix_size_ru;
  VkDeviceSize const rfs_size_ru         = ROUND_UP_POW2_MACRO(rfs_size, SPN_MEMBER_ALIGN_LIMIT);

  VkDeviceSize const rrc_size            = config->raster_builder.size.cmds * sizeof(SPN_TYPE_U32VEC4);
  VkDeviceSize const rrc_size_ru         = ROUND_UP_POW2_MACRO(rrc_size, SPN_MEMBER_ALIGN_LIMIT);

  VkDeviceSize const rfs_rrc_size_ru     = rfs_size_ru + rrc_size_ru;

  VkDeviceSize const tko_size_ru         = ROUND_UP_POW2_MACRO(rs_mr.keyvals_size, SPN_MEMBER_ALIGN_LIMIT);

  VkDeviceSize const rfs_rrc_tko_size_ru = MAX_MACRO(VkDeviceSize, rfs_rrc_size_ru, tko_size_ru);
  // clang-format on

  spinel_allocator_alloc_dbi_dm(&device->allocator.device.perm.drw,
                                device->vk.pd,
                                device->vk.d,
                                device->vk.ac,
                                max_in_flight * rfs_rrc_tko_size_ru,
                                NULL,
                                &impl->vk.dispatch.rfs_rrc_tko);

  //
  // Assign bufrefs to dispatches
  //
  // FIXME(allanmac): Do all VK objects need both their .dbis and .devaddrs
  // initialized?
  //
  for (uint32_t ii = 0; ii < max_in_flight; ii++)
    {
      struct spinel_rbi_dispatch * const dispatch = impl->dispatches.extent + ii;

      // vk.fill_scan
      spinel_dbi_devaddr_from_dbi(device->vk.d,
                                  &dispatch->vk.fill_scan,
                                  &impl->vk.dispatch.rfs_rrc_tko.dbi,
                                  ii * rfs_rrc_tko_size_ru,
                                  rfs_size_ru);

      // vk.rast_cmds
      spinel_dbi_devaddr_from_dbi(device->vk.d,
                                  &dispatch->vk.rast_cmds,
                                  &impl->vk.dispatch.rfs_rrc_tko.dbi,
                                  ii * rfs_rrc_tko_size_ru + rfs_size_ru,
                                  rrc_size_ru);

      // vk.ttrk_keyvals_odd
      spinel_dbi_devaddr_from_dbi(device->vk.d,
                                  &dispatch->vk.ttrk_keyvals_odd,
                                  &impl->vk.dispatch.rfs_rrc_tko.dbi,
                                  ii * rfs_rrc_tko_size_ru,
                                  tko_size_ru);
    }

  //
  // Initialize rings and first dispatch
  //
  spinel_ring_init(&impl->dispatches.ring, max_in_flight);

  spinel_rbi_wip_reset(impl);

  spinel_rbi_dispatch_head_init(impl);

  spinel_weakref_epoch_init(&impl->epoch);

  return SPN_SUCCESS;
}

//
//
//
