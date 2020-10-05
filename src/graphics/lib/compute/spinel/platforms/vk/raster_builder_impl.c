// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "raster_builder_impl.h"

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
#include "hotsort/platforms/vk/hotsort_vk.h"
#include "path_builder.h"
#include "path_builder_impl.h"
#include "queue_pool.h"
#include "ring.h"
#include "spinel_assert.h"
#include "status.h"
#include "vk_target.h"
#include "weakref.h"

//
// The raster builder prepares fill commands, transforms and clips for
// the rasterization sub-pipeline.
//
// A simplifying assumption is that the maximum length of a single
// raster can't be larger than what fits in the raster builder
// ring.
//
// This would be a very long raster and is a legitimate size
// limitation.
//
// If a raster is exceeds this limit then the raster builder instance
// is lost.
//
// Note that this restriction can be removed with added complexity to
// the builder and shaders.
//
// The general strategy that this particular Vulkan implementation
// uses is to allocate a large "HOST_COHERENT" buffer for the ring.
//
// Note that the maximum number of "in-flight" rasterization
// sub-pipelines is conveniently determined by the size of the fence
// pool.
//
// The size of ring buffer is driven by the desired size limit of a
// single raster.
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
// There are a maximum of 8192 rasters in a single cohort so a worst
// case allocation of single path fills would occupy 576 KB.
//
// A single raster will necessarily have a maximum number of
// paths/transforms/clips.
//
// Exceeding this limit terminates the raster builder.
//
// Note that the fills/paths count will always be 1:1 and potentially
// greater than the varying transforms/clips/rasters counts.
//
// Worst case is that the fills/transforms/clips/paths/rasters counts
// are all equal.
//
// Note that fill commands, transforms and clips may be read more than
// once by the rasterization sub-pipeline.
//
// Depending on the device architecture, it may be beneficial to copy
// the working region of the coherent buffer to a device-local buffer.
//
// If the Vulkan device is integrated or supports mapped write-through
// (AMD) then we don't need to copy.  If the device is discrete and
// doesn't support write-through (NVIDIA) then we do.
//
// Note that the fill command can reduce its transform and clip fields
// to 13-16 bits and fit into 3 dwords but... it's easier to use a
// uint4 with GPUs.
//
// A non-affine transformation elevates a Bezier to a rational.  For this
// reason, we indicate with a bit flag if the transform matrix has non-zero
// {w0,w1} elements.
//

//
// clang-format off
//

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
//
//

struct spn_cmd_fill
{
  uint32_t path_h;              // host id
  uint32_t na0            : 16; // unused
  uint32_t cohort         : 15; // cohort is 8-11 bits
  uint32_t transform_type : 1;  // transform type: 0=affine,1=projective
  uint32_t transform;           // index of first quad of transform
  uint32_t clip;                // index of clip quad
};

STATIC_ASSERT_MACRO_1(sizeof(struct spn_cmd_fill) == sizeof(uint32_t[4]));

//
// clang-format on
//

//
// There are always as many dispatch records as there are fences in
// the fence pool.  This simplifies reasoning about concurrency.
//

struct spn_rbi_span_head
{
  uint32_t span;
  uint32_t head;
};

struct spn_rbi_dispatch
{
  struct spn_rbi_span_head cf;  // fills and paths are 1:1
  struct spn_rbi_span_head tc;  // transform quads and clips
  struct spn_rbi_span_head rc;  // rasters in cohort

  bool complete;

  spn_dispatch_id_t id;
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

struct spn_rbi_vk_dbi_dm
{
  VkDescriptorBufferInfo dbi;
  VkDeviceMemory         dm;
};

struct spn_rbi_vk
{
  struct
  {
    struct
    {
      struct spn_rbi_vk_dbi_dm h;
      struct spn_rbi_vk_dbi_dm d;
    } cf;

    struct
    {
      struct spn_rbi_vk_dbi_dm h;
      struct spn_rbi_vk_dbi_dm d;
    } tc;

    struct
    {
      struct spn_rbi_vk_dbi_dm h;
      struct spn_rbi_vk_dbi_dm d;
    } rc;
  } rings;

  struct
  {
    VkDescriptorBufferInfo dbi;
    VkDeviceMemory         dm;
  } copyback;
};

//
//
//

struct spn_raster_builder_impl
{
  struct spn_raster_builder *         raster_builder;
  struct spn_device *                 device;
  struct spn_vk_target_config const * config;
  struct spn_rbi_vk                   vk;

  //
  // As noted above, the remaining slots in the fills ring is always
  // greater-than-or-equal to the remaining slots in the tcs ring so
  // we use simpler accounting for tcs and rc.
  //
  struct
  {
    struct
    {
      struct spn_cmd_fill * extent;
      struct spn_ring       ring;
    } cf;  // fill commands

    struct
    {
      struct spn_vec4 * extent;
      struct spn_next   next;
    } tc;  // transforms & clips

    struct
    {
      spn_handle_t *  extent;
      struct spn_next next;
    } rc;  // rasters in cohort

    struct
    {
      uint32_t * extent;
    } cb;  // ttrk key count copyback indexed by dispatch idx
  } mapped;

  //
  // work in progress raster
  //
  struct
  {
    struct
    {
      uint32_t span;
    } cf;  // fills

    struct
    {
      uint32_t span;
    } tc;  // transforms and clips
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
  spn_weakref_epoch_t epoch;

  struct
  {
    spn_handle_t * extent;
  } paths;

  struct
  {
    spn_handle_t * extent;
  } rasters;

  struct
  {
    struct spn_rbi_dispatch * extent;
    struct spn_ring           ring;
  } dispatches;
};

//
//
//

static bool
spn_rbi_is_staged(struct spn_vk_target_config const * const config)
{
  return ((config->allocator.device.hw_dr.properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0) &&
         (config->raster_builder.no_staging == 0);
}

//
//
//

static spn_result_t
spn_rbi_lost_begin(struct spn_raster_builder_impl * const impl)
{
  return SPN_ERROR_RASTER_BUILDER_LOST;
}

static spn_result_t
spn_rbi_lost_end(struct spn_raster_builder_impl * const impl, spn_raster_t * const raster)
{
  *raster = SPN_RASTER_INVALID;  // FIXME -- SPN_TYPED_HANDLE_INVALID

  return SPN_ERROR_RASTER_BUILDER_LOST;
}

static spn_result_t
spn_rbi_release(struct spn_raster_builder_impl * const impl);

static spn_result_t
spn_rbi_lost_release(struct spn_raster_builder_impl * const impl)
{
  //
  // FIXME -- releasing a lost path builder might eventually require a
  // specialized function.  For now, just call the default release.
  //
  return spn_rbi_release(impl);
}

static spn_result_t
spn_rbi_lost_flush(struct spn_raster_builder_impl * const impl)
{
  return SPN_ERROR_RASTER_BUILDER_LOST;
}

static spn_result_t
spn_rbi_lost_add(struct spn_raster_builder_impl * const impl,
                 spn_path_t const *                     paths,
                 spn_transform_weakref_t *              transform_weakrefs,
                 spn_transform_t const *                transforms,
                 spn_clip_weakref_t *                   clip_weakrefs,
                 spn_clip_t const *                     clips,
                 uint32_t                               count)
{
  return SPN_ERROR_RASTER_BUILDER_LOST;
}

//
// If (wip.span == mapped.ring.size) then the raster is too long and
// the raster builder is terminally "lost".  The raster builder should
// be released and a new one created.
//

static void
spn_rbi_lost(struct spn_raster_builder_impl * const impl)
{
  struct spn_raster_builder * const rb = impl->raster_builder;

  rb->begin   = spn_rbi_lost_begin;
  rb->end     = spn_rbi_lost_end;
  rb->release = spn_rbi_lost_release;
  rb->flush   = spn_rbi_lost_flush;
  rb->add     = spn_rbi_lost_add;
}

static void
spn_rbi_raster_append(struct spn_raster_builder_impl * const impl,
                      spn_raster_t const * const             raster)
{
  uint32_t const idx = spn_next_acquire_1(&impl->mapped.rc.next);

  // device
  impl->mapped.rc.extent[idx] = raster->handle;

  // host
  impl->rasters.extent[idx] = raster->handle;
}

//
// A dispatch captures how many paths and blocks are in a dispatched or
// the work-in-progress compute grid.
//

static struct spn_rbi_dispatch *
spn_rbi_dispatch_idx(struct spn_raster_builder_impl * const impl, uint32_t const idx)
{
  return impl->dispatches.extent + idx;
}

static struct spn_rbi_dispatch *
spn_rbi_dispatch_head(struct spn_raster_builder_impl * const impl)
{
  return spn_rbi_dispatch_idx(impl, impl->dispatches.ring.head);
}

static struct spn_rbi_dispatch *
spn_rbi_dispatch_tail(struct spn_raster_builder_impl * const impl)
{
  return spn_rbi_dispatch_idx(impl, impl->dispatches.ring.tail);
}

//
//
//

static void
spn_rbi_dispatch_init(struct spn_raster_builder_impl * const impl,
                      struct spn_rbi_dispatch * const        dispatch)
{
  dispatch->cf.span = 0;
  dispatch->cf.head = impl->mapped.cf.ring.head;

  dispatch->tc.span = 0;
  dispatch->tc.head = impl->mapped.tc.next.head;

  dispatch->rc.span = 0;
  dispatch->rc.head = impl->mapped.rc.next.head;

  dispatch->complete = false;

  spn(device_dispatch_acquire(impl->device, SPN_DISPATCH_STAGE_RASTER_BUILDER_2, &dispatch->id));

  spn_device_dispatch_set_flush_arg(impl->device, dispatch->id, impl);
}

static void
spn_rbi_dispatch_drop(struct spn_raster_builder_impl * const impl)
{
  struct spn_ring * const ring = &impl->dispatches.ring;

  spn_ring_drop_1(ring);
};

static void
spn_rbi_dispatch_acquire(struct spn_raster_builder_impl * const impl)
{
  struct spn_ring * const ring = &impl->dispatches.ring;

  while (spn_ring_is_empty(ring))
    {
      spn(device_wait(impl->device, __func__));
    }

  struct spn_rbi_dispatch * const dispatch = spn_rbi_dispatch_head(impl);

  spn_rbi_dispatch_init(impl, dispatch);
}

static void
spn_rbi_dispatch_append(struct spn_raster_builder_impl * const impl,
                        struct spn_rbi_dispatch * const        dispatch,
                        spn_raster_t const * const             raster)
{
  dispatch->cf.span += impl->wip.cf.span;
  dispatch->tc.span += impl->wip.tc.span;
  dispatch->rc.span += 1;
}

static bool
spn_rbi_is_wip_dispatch_empty(struct spn_rbi_dispatch const * const dispatch)
{
  return dispatch->rc.span == 0;
}

//
// RASTERIZATION PAYLOAD PHASE 2
//

struct spn_rbi_complete_payload_2
{
  struct spn_raster_builder_impl * impl;

  struct
  {
    struct spn_vk_ds_ttrks_t      t;
    struct spn_vk_ds_raster_ids_t i;
  } ds;

  struct
  {
    spn_subbuf_id_t ttrks;
  } temp;

  uint32_t dispatch_idx;
};

//
// RASTERIZATION PAYLOAD PHASE 1
//

struct spn_rbi_complete_payload_1
{
  struct spn_raster_builder_impl * impl;

  struct
  {
    struct spn_vk_ds_rasterize_t r;
    struct spn_vk_ds_ttrks_t     t;
  } ds;

  struct
  {
    spn_subbuf_id_t fill_scan;
    spn_subbuf_id_t rast_cmds;
    spn_subbuf_id_t ttrks;
  } temp;

  uint32_t dispatch_idx;
};

//
//
//

static void
spn_rbi_complete_2(void * pfn_payload)
{
  struct spn_rbi_complete_payload_2 const * const payload_2 = pfn_payload;
  struct spn_raster_builder_impl * const          impl      = payload_2->impl;
  struct spn_device * const                       device    = impl->device;
  struct spn_vk * const                           instance  = device->instance;

  // release the rasterize ds
  spn_vk_ds_release_ttrks(instance, payload_2->ds.t);
  spn_vk_ds_release_raster_ids(instance, payload_2->ds.i);

  // release the rasterize post temp buffer -- will never wait()
  spn_allocator_device_temp_free(&device->allocator.device.temp.drw, payload_2->temp.ttrks);

  //
  // get the dispatch record
  //
  uint32_t const            dispatch_idx = payload_2->dispatch_idx;
  struct spn_rbi_dispatch * dispatch     = spn_rbi_dispatch_idx(impl, dispatch_idx);

  //
  // These raster handles are now materialized
  //
  spn_device_dispatch_handles_complete(device,
                                       impl->rasters.extent,
                                       impl->mapped.rc.next.size,
                                       dispatch->rc.head,
                                       dispatch->rc.span);

  //
  // Release the rasters -- may invoke wait()
  //
  spn_device_handle_pool_release_ring_d_rasters(device,
                                                impl->rasters.extent,
                                                impl->mapped.rc.next.size,
                                                dispatch->rc.head,
                                                dispatch->rc.span);
  //
  // If the dispatch is the tail of the ring then try to release as
  // many dispatch records as possible...
  //
  // Note that kernels can complete in any order so the release
  // records need to add to the mapped.ring.tail in order.
  //
  if (spn_ring_is_tail(&impl->dispatches.ring, dispatch_idx))
    {
      while (true)
        {
          spn_ring_release_n(&impl->mapped.cf.ring, dispatch->cf.span);
          spn_ring_release_n(&impl->dispatches.ring, 1);

          // any dispatches in flight?
          if (spn_ring_is_full(&impl->dispatches.ring))
            break;

          dispatch = spn_rbi_dispatch_tail(impl);

          if (!dispatch->complete)
            break;
        }
    }
  else
    {
      dispatch->complete = true;
    }
}

//
//
//

static void
spn_rbi_complete_1(void * pfn_payload)
{
  struct spn_rbi_complete_payload_1 const * const payload_1 = pfn_payload;
  struct spn_raster_builder_impl * const          impl      = payload_1->impl;
  struct spn_device * const                       device    = impl->device;

  //
  // Release the two temp buffers used by phase 1
  //
  spn_allocator_device_temp_free(&device->allocator.device.temp.drw, payload_1->temp.fill_scan);
  spn_allocator_device_temp_free(&device->allocator.device.temp.drw, payload_1->temp.rast_cmds);

  //
  // Release the rasterize ds
  //
  struct spn_vk * const instance = device->instance;

  spn_vk_ds_release_rasterize(instance, payload_1->ds.r);

  //
  // Command Buffer 2
  // ----------------
  //
  //   2.1) HOTSORT TTRK KEYS
  //
  //   2.2) SEGMENT_TTRK
  //
  //   2.3) RASTERS_ALLOC
  //
  //   2.4) RASTERS_PREFIX
  //
  struct spn_rbi_dispatch * const dispatch = spn_rbi_dispatch_idx(impl, payload_1->dispatch_idx);

  //
  // Acquire callback state
  //
  struct spn_rbi_complete_payload_2 * const payload_2 =
    spn_device_dispatch_set_completion(device,
                                       dispatch->id,
                                       spn_rbi_complete_2,
                                       sizeof(*payload_2));

  payload_2->impl         = payload_1->impl;
  payload_2->ds.t         = payload_1->ds.t;
  payload_2->temp.ttrks   = payload_1->temp.ttrks;
  payload_2->dispatch_idx = payload_1->dispatch_idx;

  //
  // Acquire the cb
  //
  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, dispatch->id);

  //
  // DS: BLOCK_POOL
  //
  spn_vk_ds_bind_segment_ttrk_block_pool(instance, cb, spn_device_block_pool_get_ds(device));

  //
  // DS: TTRKS
  //
  spn_vk_ds_bind_segment_ttrk_ttrks(instance, cb, payload_2->ds.t);

  //
  // DS: RASTER_IDS
  //
  spn_vk_ds_acquire_raster_ids(instance, device, &payload_2->ds.i);

  // dbi: raster_ids
  *spn_vk_ds_get_raster_ids_raster_ids(instance, payload_2->ds.i) = impl->vk.rings.rc.d.dbi;

  // update raster_ids ds
  spn_vk_ds_update_raster_ids(instance, &device->environment, payload_2->ds.i);

  // bind raster_ids ds
  spn_vk_ds_bind_rasters_alloc_raster_ids(instance, cb, payload_2->ds.i);

  ////////////////////////////////////////////////////////////////
  //
  // HOTSORT
  //
  ////////////////////////////////////////////////////////////////

  if ((impl->config->allocator.device.hr_dw.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
      VkMappedMemoryRange const mmr = { .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                        .pNext  = NULL,
                                        .memory = impl->vk.copyback.dm,
                                        .offset = 0,
                                        .size   = VK_WHOLE_SIZE };

      vk(InvalidateMappedMemoryRanges(device->environment.d, 1, &mmr));
    }

  uint32_t const keys_count = impl->mapped.cb.extent[payload_2->dispatch_idx];

  uint32_t slabs_in;
  uint32_t padded_in;
  uint32_t padded_out;

  hotsort_vk_pad(device->hs, keys_count, &slabs_in, &padded_in, &padded_out);

  struct hotsort_vk_ds_offsets const keys_offsets = {
    .in  = SPN_VK_BUFFER_OFFSETOF(ttrks, ttrks, ttrks_keys),
    .out = SPN_VK_BUFFER_OFFSETOF(ttrks, ttrks, ttrks_keys)
  };

#if !defined(NDEBUG) && 0
  fprintf(stderr,
          "Raster Builder:\n"
          "  keys_count       = %u\n"
          "  slabs_in         = %u\n"
          "  padded_in        = %u\n"
          "  padded_out       = %u\n"
          "  keys_offsets.in  = %zu\n"
          "  keys_offsets.out = %zu\n",
          keys_count,
          slabs_in,
          padded_in,
          padded_out,
          keys_offsets.in,
          keys_offsets.out);
#endif

  hotsort_vk_sort(cb, device->hs, &keys_offsets, keys_count, padded_in, padded_out, false);

  vk_barrier_compute_w_to_compute_r(cb);

  //
  //   2.2) SEGMENT_TTRK
  //
  //   2.3) RASTERS_ALLOC
  //
  //   2.4) RASTERS_PREFIX
  //

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: SEGMENT_TTRK
  //
  ////////////////////////////////////////////////////////////////

  //
  // TODO(allanmac): evaluate whether or not to remove this conditional
  // once fxb:50840 is resolved.
  //
  if (slabs_in > 0)
    {
      // bind the pipeline
      spn_vk_p_bind_segment_ttrk(instance, cb);

      // dispatch one subgroup (workgroup) per slab
      vkCmdDispatch(cb, slabs_in, 1, 1);

      // compute barrier
      vk_barrier_compute_w_to_compute_r(cb);
    }

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: RASTERS_ALLOC
  //
  ////////////////////////////////////////////////////////////////

  struct spn_vk_push_rasters_alloc const push_rasters_alloc = {
    .bp_mask     = spn_device_block_pool_get_mask(device),
    .raster_span = dispatch->rc.span,
    .raster_head = dispatch->rc.head,
    .raster_size = impl->mapped.rc.next.size
  };

  // bind the push constants
  spn_vk_p_push_rasters_alloc(instance, cb, &push_rasters_alloc);

  // bind the pipeline
  spn_vk_p_bind_rasters_alloc(instance, cb);

  // dispatch one thread per raster rounded up to a workgroup
  uint32_t const ra_wg_size = impl->config->p.group_sizes.named.rasters_alloc.workgroup;
  uint32_t const ra_wgs     = (dispatch->rc.span + ra_wg_size - 1) / ra_wg_size;

  vkCmdDispatch(cb, ra_wgs, 1, 1);

  // compute barrier
  vk_barrier_compute_w_to_compute_r(cb);

#if !defined(NDEBUG) && 0
  fprintf(stderr, "dispatch->rc.span = { %u, %u }\n", dispatch->rc.span, ra_wgs);
#endif

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: RASTERS_PREFIX
  //
  ////////////////////////////////////////////////////////////////

  // push constants remain the same

  // bind the pipeline
  spn_vk_p_bind_rasters_prefix(instance, cb);

  // dispatch one subgroup (workgroup) per raster
  vkCmdDispatch(cb, dispatch->rc.span, 1, 1);

  //
  // submit the dispatch
  //
  spn_device_dispatch_submit(device, dispatch->id);

  //
  // Release paths after submitting the phase 2 command buffer to
  // reduce latency since this might result in many PATHS_RELEASE
  // shaders being launched.
  //
  spn_device_handle_pool_release_ring_d_paths(device,
                                              impl->paths.extent,
                                              impl->mapped.cf.ring.size,
                                              dispatch->cf.head,
                                              dispatch->cf.span);
}

//
//
//

static void
spn_rbi_copy_ring(VkCommandBuffer                        cb,
                  struct spn_rbi_vk_dbi_dm const * const h,
                  struct spn_rbi_vk_dbi_dm const * const d,
                  VkDeviceSize const                     elem_size,
                  uint32_t const                         ring_size,
                  struct spn_rbi_span_head const * const span_head)
{
  VkBufferCopy bcs[2];
  uint32_t     bc_count;

  bool const     is_wrap   = (span_head->span + span_head->head) > ring_size;
  uint32_t const span_hi   = is_wrap ? (ring_size - span_head->head) : span_head->span;
  VkDeviceSize   offset_hi = elem_size * span_head->head;

  bcs[0].srcOffset = h->dbi.offset + offset_hi;
  bcs[0].dstOffset = d->dbi.offset + offset_hi;
  bcs[0].size      = elem_size * span_hi;

  if (is_wrap)
    {
      uint32_t const span_lo = span_head->span - span_hi;

      bcs[1].srcOffset = h->dbi.offset;
      bcs[1].dstOffset = d->dbi.offset;
      bcs[1].size      = elem_size * span_lo;

      bc_count = 2;
    }
  else
    {
      bc_count = 1;
    }

  vkCmdCopyBuffer(cb, h->dbi.buffer, d->dbi.buffer, bc_count, bcs);
}

//
//
//

spn_result_t
spn_rbi_flush(struct spn_raster_builder_impl * const impl)
{
  struct spn_rbi_dispatch const * const dispatch = spn_rbi_dispatch_head(impl);

  // anything to launch?
  if (spn_rbi_is_wip_dispatch_empty(dispatch))
    return SPN_SUCCESS;

  // invalidate all outstanding transform and clip weakrefs
  spn_weakref_epoch_increment(&impl->epoch);

  //
  // We're go for launch...
  //
  // The rasterization sub-pipeline is fairly intricate and submits a
  // command buffer that, upon completion, submits a second command
  // buffer.
  //
  // The second command buffer is launched by a callback because we
  // need to know how many keys were produced by the rasterization
  // shader(s).
  //
  // NOTE: Ideally we will have a dedicated hot thread for handling
  // the first command buffer's completion and launch of the second
  // but that is a surgical and non-trivial improvement that can be
  // made later.
  //
  // Command Buffer 1
  // ----------------
  //
  // The first command buffer rasterizes the fill commands and saves
  // the generated TTRK keys in a temporary buffer.
  //
  //   1.1) FILLS_SCAN
  //
  //        Compute the prefix sum of each path type in the fill's path.
  //
  //   1.2) FILLS_DISPATCH
  //
  //        Take the atomically updated count of rasterization commands
  //        and initialize a workgroup triple for
  //        vkCmdDispatchIndirect().
  //
  //   1.3) FILLS_EXPAND
  //
  //        Expand the fill command into rasterization commands and
  //        store them to a temporary buffer:
  //
  //          |<lines><quads><cubics><rat_quads><rat_cubics>|
  //
  //   1.4) RASTERIZE_LINES/QUADS/CUBICS/RAT_QUADS/RAT_CUBICS
  //
  //        For each path type, indirectly dispatch a rasterizer.
  //
  //   1.5) COPY TTRK KEYS COUNT TO HOST
  //
  // Callback
  // --------
  //
  // Command Buffer 2
  // ----------------
  //
  //   2.1) HOTSORT TTRK KEYS
  //
  //   2.2) SEGMENT_TTRK
  //
  //   2.3) RASTERS_ALLOC
  //
  //   2.4) RASTERS_PREFIX
  //
  struct spn_device * const device = impl->device;

  // reset the flush arg associated with the dispatch id
  spn_device_dispatch_reset_flush_arg(device, dispatch->id);

  ////////////////////////////////////////////////////////////////
  //
  // COMMAND BUFFER 1
  //
  ////////////////////////////////////////////////////////////////

  //
  // The dispatch associated with the WIP is used for the second
  // submission.
  //
  // We need to acquire a new dispatch for the first stage.
  //
  spn_dispatch_id_t id_1;

  spn(device_dispatch_acquire(device, SPN_DISPATCH_STAGE_RASTER_BUILDER_1, &id_1));

  //
  // Acquire callback state
  //
  struct spn_rbi_complete_payload_1 * const payload_1 =
    spn_device_dispatch_set_completion(device, id_1, spn_rbi_complete_1, sizeof(*payload_1));

  payload_1->impl         = impl;
  payload_1->dispatch_idx = impl->dispatches.ring.head;

  // first command buffer
  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, id_1);

  ////////////////////////////////////////////////////////////////
  //
  // DS: BLOCK_POOL
  //
  // bind the global BLOCK_POOL descriptor set
  //
  ////////////////////////////////////////////////////////////////

  struct spn_vk * const instance = device->instance;

  spn_vk_ds_bind_fills_scan_block_pool(instance, cb, spn_device_block_pool_get_ds(device));

  ////////////////////////////////////////////////////////////////
  //
  // DS: RASTERIZE
  //
  ////////////////////////////////////////////////////////////////

  spn_vk_ds_acquire_rasterize(instance, device, &payload_1->ds.r);

  // dbi: fill_cmds
  *spn_vk_ds_get_rasterize_fill_cmds(instance, payload_1->ds.r) = impl->vk.rings.cf.d.dbi;

  // dbi: fill_quads
  *spn_vk_ds_get_rasterize_fill_quads(instance, payload_1->ds.r) = impl->vk.rings.tc.d.dbi;

  // dbi: fill_scan -- allocate a temporary buffer
  VkDescriptorBufferInfo * const dbi_fill_scan =
    spn_vk_ds_get_rasterize_fill_scan(instance, payload_1->ds.r);

  // fill_scan_prefix[] "blocked" layout requires padding
  uint32_t const fill_scan_subgroup_mask =
    BITS_TO_MASK_MACRO(impl->config->p.group_sizes.named.fills_scan.subgroup_log2);

  uint32_t const dispatch_cf_span_ru =
    (dispatch->cf.span + fill_scan_subgroup_mask) & ~fill_scan_subgroup_mask;

  spn_allocator_device_temp_alloc(&device->allocator.device.temp.drw,
                                  device,
                                  spn_device_wait,
                                  SPN_VK_BUFFER_OFFSETOF(rasterize, fill_scan, fill_scan_prefix) +
                                    dispatch_cf_span_ru * sizeof(SPN_TYPE_UVEC4) * 2,
                                  &payload_1->temp.fill_scan,
                                  dbi_fill_scan);

  // dbi: rast_cmds -- allocate a temporary buffer
  VkDescriptorBufferInfo * const dbi_rast_cmds =
    spn_vk_ds_get_rasterize_rast_cmds(instance, payload_1->ds.r);

  spn_allocator_device_temp_alloc(&device->allocator.device.temp.drw,
                                  device,
                                  spn_device_wait,
                                  SPN_VK_BUFFER_OFFSETOF(rasterize, rast_cmds, rast_cmds) +
                                    impl->config->raster_builder.size.cmds * sizeof(SPN_TYPE_UVEC4),
                                  &payload_1->temp.rast_cmds,
                                  dbi_rast_cmds);

  // update rasterize ds
  spn_vk_ds_update_rasterize(instance, &device->environment, payload_1->ds.r);

  // bind rasterize ds
  spn_vk_ds_bind_fills_scan_rasterize(instance, cb, payload_1->ds.r);

  ////////////////////////////////////////////////////////////////
  //
  // DS: TTRKS
  //
  ////////////////////////////////////////////////////////////////

  spn_vk_ds_acquire_ttrks(instance, device, &payload_1->ds.t);

  VkDescriptorBufferInfo * const dbi_ttrks = spn_vk_ds_get_ttrks_ttrks(instance, payload_1->ds.t);

  // dbi: ttrks -- allocate a temporary buffer
  VkDeviceSize const ttrks_size = SPN_VK_BUFFER_OFFSETOF(ttrks, ttrks, ttrks_keys) +
                                  impl->config->raster_builder.size.ttrks * sizeof(SPN_TYPE_UVEC2);

  spn_allocator_device_temp_alloc(&device->allocator.device.temp.drw,
                                  device,
                                  spn_device_wait,
                                  ttrks_size,
                                  &payload_1->temp.ttrks,
                                  dbi_ttrks);

  // update ttrks ds
  spn_vk_ds_update_ttrks(instance, &device->environment, payload_1->ds.t);

  // bind ttrks ds
  spn_vk_ds_bind_rasterize_line_ttrks(instance, cb, payload_1->ds.t);

  ////////////////////////////////////////////////////////////////
  //
  // FILL: RASTER COHORT META TABLE
  //
  ////////////////////////////////////////////////////////////////

  {
    //
    // zero ttrks.ttrks_meta.[rk_off|blocks|ttpks|ttrks]
    //
    // NOTE(allanmac): This fill has no dependencies until stage 2
    //
    VkDeviceSize const offset = SPN_VK_BUFFER_OFFSETOF(ttrks, ttrks, ttrks_meta.rk_off);
    VkDeviceSize const size   = SPN_VK_BUFFER_MEMBER_SIZE(ttrks, ttrks, ttrks_meta) - offset;

    vkCmdFillBuffer(cb, dbi_ttrks->buffer, dbi_ttrks->offset + offset, size, 0);
  }

  ////////////////////////////////////////////////////////////////
  //
  // COPY: COMMAND RINGS
  //
  // On a discrete GPU, 1-2 regions of 3 rings are copied from H>D
  //
  ////////////////////////////////////////////////////////////////

  if (spn_rbi_is_staged(impl->config))
    {
      // CF
      spn_rbi_copy_ring(cb,
                        &impl->vk.rings.cf.h,
                        &impl->vk.rings.cf.d,
                        sizeof(*impl->mapped.cf.extent),
                        impl->mapped.cf.ring.size,
                        &dispatch->cf);

      // TC
      spn_rbi_copy_ring(cb,
                        &impl->vk.rings.tc.h,
                        &impl->vk.rings.tc.d,
                        sizeof(*impl->mapped.tc.extent),
                        impl->mapped.tc.next.size,
                        &dispatch->tc);

      // RC
      spn_rbi_copy_ring(cb,
                        &impl->vk.rings.rc.h,
                        &impl->vk.rings.rc.d,
                        sizeof(*impl->mapped.rc.extent),
                        impl->mapped.rc.next.size,
                        &dispatch->rc);
    }

  ////////////////////////////////////////////////////////////////
  //
  // FILL: ZERO RASTERIZE.FILL_SCAN_COUNTS and TTRKS.COUNT
  //
  ////////////////////////////////////////////////////////////////

  // zero the rasterize.fill_scan_counts member
  {
    VkDeviceSize const offset = SPN_VK_BUFFER_OFFSETOF(rasterize, fill_scan, fill_scan_counts);
    VkDeviceSize const size   = SPN_VK_BUFFER_MEMBER_SIZE(rasterize, fill_scan, fill_scan_counts);

    vkCmdFillBuffer(cb, dbi_fill_scan->buffer, dbi_fill_scan->offset + offset, size, 0);
  }

  // zero the ttrks_count member
  {
    VkDeviceSize const offset = SPN_VK_BUFFER_OFFSETOF(ttrks, ttrks, ttrks_count);
    VkDeviceSize const size   = SPN_VK_BUFFER_MEMBER_SIZE(ttrks, ttrks, ttrks_count);

    vkCmdFillBuffer(cb, dbi_ttrks->buffer, dbi_ttrks->offset + offset, size, 0);
  }

  ////////////////////////////////////////////////////////////////
  //
  // BARRIER FOR BOTH FILLS
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_transfer_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: FILLS_SCAN
  //
  ////////////////////////////////////////////////////////////////

  struct spn_vk_push_fills_scan const push_fills_scan = {

    .bp_mask  = spn_device_block_pool_get_mask(device),
    .cmd_span = dispatch->cf.span,
    .cmd_head = dispatch->cf.head,
    .cmd_size = impl->mapped.cf.ring.size
  };

  // bind the push constants
  spn_vk_p_push_fills_scan(instance, cb, &push_fills_scan);

  // bind the pipeline
  spn_vk_p_bind_fills_scan(instance, cb);

  // size the grid
  uint32_t const cmds_per_wg = impl->config->raster_builder.fills_scan.rows *
                               impl->config->p.group_sizes.named.fills_scan.workgroup;

  uint32_t const wg_count = (dispatch->cf.span + cmds_per_wg - 1) / cmds_per_wg;

#if !defined(NDEBUG) && 0
  fprintf(stderr,
          "<<< [ %5u, %5u, %5u, %5u ] ( %08X , %08X , %08X, %08X ) <<< %5u, 1, 1 >>>\n",
          impl->dispatches.ring.size,
          impl->dispatches.ring.head,
          impl->dispatches.ring.tail,
          impl->dispatches.ring.rem,
          dispatch->cf.span,
          dispatch->cf.head,
          (dispatch->cf.head + dispatch->cf.span) % impl->mapped.cf.ring.size,
          impl->mapped.cf.ring.size,
          wg_count);
#endif

  vkCmdDispatch(cb, wg_count, 1, 1);

  // compute barrier
  vk_barrier_compute_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: FILLS_DISPATCH
  //
  ////////////////////////////////////////////////////////////////

  // no need to set up push constants since they're identical to
  // FILLS_SCAN and therefore compatible

  // bind the pipeline
  spn_vk_p_bind_fills_dispatch(instance, cb);

  // a single workgroup initialize the indirect dispatches
  vkCmdDispatch(cb, 1, 1, 1);

  // compute barrier
  vk_barrier_compute_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: FILLS_EXPAND
  //
  ////////////////////////////////////////////////////////////////

  // no need to set up push constants since they're identical to
  // FILLS_SCAN and therefore compatible

  // bind the pipeline
  spn_vk_p_bind_fills_expand(instance, cb);

  //
  // FIXME(allanmac): size the grid based on workgroup/subgroup
  //

  // dispatch one workgroup per fill command
  vkCmdDispatch(cb, dispatch->cf.span, 1, 1);

  // indirect compute barrier
  vk_barrier_compute_w_to_indirect_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // SHADERS: RASTERIZE_[LINES|QUADS|CUBICS|RAT_QUADS|RAT_CUBICS]
  //
  ////////////////////////////////////////////////////////////////

  //
  // FIXME(allanmac): The indirect dispatch may need to handle
  // workgroups larger than one subgroup.
  //
#define SPN_VK_P_BIND_RASTERIZE_NAME(_p) spn_vk_p_bind_rasterize_##_p

#undef SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X
#define SPN_RASTER_BUILDER_RAST_TYPE_EXPAND_X(_p, _i, _n)                                          \
  SPN_VK_P_BIND_RASTERIZE_NAME(_p)(instance, cb);                                                  \
  vkCmdDispatchIndirect(cb,                                                                        \
                        dbi_fill_scan->buffer,                                                     \
                        dbi_fill_scan->offset +                                                    \
                          SPN_VK_BUFFER_OFFSETOF(rasterize, fill_scan, fill_scan_dispatch) +       \
                          sizeof(SPN_TYPE_UVEC4) * _i);

  SPN_RASTER_BUILDER_RAST_TYPE_EXPAND()

  ////////////////////////////////////////////////////////////////
  //
  // RASTERIZATION COMPLETE -- copyback ttrk count
  //
  // FIXME(allanmac): This is such small amount of data that setting
  // up a transfer is probably non-performant.  It has been my
  // experience that a custom copying compute shader will greatly
  // outperform the transfer for small copies.
  //
  // For now, leave it as is until we can observe actual runtimes with a
  // vendor-specific Vulkan profiling application... and note that some
  // vendors already implement their copying routines with compute
  // shaders.
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_transfer_r(cb);

  VkDescriptorBufferInfo const * const dbi_copyback = &impl->vk.copyback.dbi;

  VkBufferCopy const bc = {

    .srcOffset = dbi_ttrks->offset + SPN_VK_BUFFER_OFFSETOF(ttrks, ttrks, ttrks_count),
    .dstOffset = dbi_copyback->offset + sizeof(*impl->mapped.cb.extent) * payload_1->dispatch_idx,
    .size      = sizeof(*impl->mapped.cb.extent)
  };

  vkCmdCopyBuffer(cb, dbi_ttrks->buffer, dbi_copyback->buffer, 1, &bc);

  //
  // make the copyback visible to the host
  //
  vk_barrier_transfer_w_to_host_r(cb);

  //
  // the current dispatch is now sealed so drop it
  //
  spn_rbi_dispatch_drop(impl);

  //
  // Declare that this dispatch can't start until the path handles are
  // materialized
  //
  spn_device_dispatch_happens_after_handles_and_submit(device,
                                                       (spn_dispatch_flush_pfn_t)spn_pbi_flush,
                                                       id_1,
                                                       impl->paths.extent,
                                                       impl->mapped.cf.ring.size,
                                                       dispatch->cf.head,
                                                       dispatch->cf.span);

  //
  // acquire and initialize the next dispatch
  //
  spn_rbi_dispatch_acquire(impl);

  return SPN_SUCCESS;
}

//
// We record where the *next* work-in-progress path will start in the
// ring along with its rolling counter.
//

static void
spn_rbi_wip_init(struct spn_raster_builder_impl * const impl)
{
  impl->wip.cf.span = 0;
  impl->wip.tc.span = 0;
}

//
//
//

static spn_result_t
spn_rbi_begin(struct spn_raster_builder_impl * const impl)
{
  return SPN_SUCCESS;
}

//
//
//

static spn_result_t
spn_rbi_end(struct spn_raster_builder_impl * const impl, spn_raster_t * const raster)
{
  // acquire raster host id
  spn_device_handle_pool_acquire(impl->device, &raster->handle);

  // get the head dispatch
  struct spn_rbi_dispatch * const dispatch = spn_rbi_dispatch_head(impl);

  // register raster handle with wip dispatch
  spn_device_dispatch_register_handle(impl->device, dispatch->id, raster->handle);

  // save raster to ring
  spn_rbi_raster_append(impl, raster);

  // update head dispatch record
  spn_rbi_dispatch_append(impl, dispatch, raster);

  // start a new wip
  spn_rbi_wip_init(impl);

  //
  // FIXME(allanmac): flush eagerly
  //
#if 0
  if (spn_rbi_dispatch_head(impl)->blocks.span >= impl->config.raster_builder.size.eager)
    ;
#endif

  //
  // flush if the cohort size limit has been reached
  //
  bool const is_full = (dispatch->rc.span == impl->config->raster_builder.size.cohort);

  if (is_full)
    {
      return spn_rbi_flush(impl);
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
// invalidated by about-to-happen spn_rbi_flush().
//

static spn_result_t
spn_rbi_validate_transform_weakref_indices(struct spn_ring const * const         cf_ring,
                                           struct spn_rbi_dispatch const * const dispatch,
                                           spn_transform_weakref_t const * const transform_weakrefs,
                                           uint32_t const                        count)
{
  //
  // FIXME(allanmac)
  //
  // For non-null weakrefs, check to see index is within WIP span
  //
  return SPN_SUCCESS;
}

static spn_result_t
spn_rbi_validate_clip_weakref_indices(struct spn_ring const * const         cf_ring,
                                      struct spn_rbi_dispatch const * const dispatch,
                                      spn_clip_weakref_t const * const      clip_weakrefs,
                                      uint32_t const                        count)
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
//
// src: { sx shx tx  shy sy ty w0 w1 } // row-ordered matrix
// dst: { sx shx shy sy  tx ty w0 w1 } // GPU-friendly ordering
//

static void
spn_rbi_transform_copy_lo(struct spn_vec4 * const dst, spn_transform_t const * const src)
{
  dst->x = src->sx;
  dst->y = src->shx;
  dst->z = src->shy;
  dst->w = src->sy;
}

static void
spn_rbi_transform_copy_hi(struct spn_vec4 * const dst, spn_transform_t const * const src)
{
  dst->x = src->tx;
  dst->y = src->ty;
  dst->z = src->w0;
  dst->w = src->w1;
}

//
//
//

static spn_result_t
spn_rbi_add(struct spn_raster_builder_impl * const impl,
            spn_path_t const *                     paths,
            spn_transform_weakref_t *              transform_weakrefs,
            spn_transform_t const *                transforms,
            spn_clip_weakref_t *                   clip_weakrefs,
            spn_clip_t const *                     clips,
            uint32_t                               count)
{
  // anything to do?
  if (count == 0)
    {
      return SPN_SUCCESS;
    }

  //
  // If the number of paths is larger than the ring then fail!
  //
  struct spn_ring * const cf_ring = &impl->mapped.cf.ring;

  if (count > cf_ring->size)
    {
      return SPN_ERROR_RASTER_BUILDER_TOO_MANY_PATHS;
    }

  //
  // If not enough entries are left in the command ring then flush now!
  //
  struct spn_rbi_dispatch * const dispatch = spn_rbi_dispatch_head(impl);

  if (count > cf_ring->rem)
    {
      // if dispatch is empty and the work-in-progress is going to
      // exceed the size of the ring then this is a fatal error. At
      // this point, we can kill the raster builder instead of the
      // device.
      if (spn_rbi_is_wip_dispatch_empty(dispatch) || (impl->wip.cf.span + count > cf_ring->size))
        {
          spn_rbi_lost(impl);

          return SPN_ERROR_RASTER_BUILDER_LOST;
        }

      //
      // otherwise, launch whatever is in the ring
      //
      spn_rbi_flush(impl);

      //
      // ... and wait for space
      //
      do
        {
          spn(device_wait(impl->device, __func__));
      } while (cf_ring->rem < count);
    }

  //
  // validate the paths before we proceed
  //
  spn_result_t result;

  result = spn_device_handle_pool_validate_d_paths(impl->device, paths, count);

  if (result != SPN_SUCCESS)
    return result;

  //
  // validate the transform and clip weakref indices -- this is cheap!
  //
  result = spn_rbi_validate_transform_weakref_indices(cf_ring, dispatch, transform_weakrefs, count);

  if (result != SPN_SUCCESS)
    return result;

  result = spn_rbi_validate_clip_weakref_indices(cf_ring, dispatch, clip_weakrefs, count);

  if (result != SPN_SUCCESS)
    return result;

  //
  // everything validates... retain the paths on the device
  //
  spn_device_handle_pool_retain_d_paths(impl->device, paths, count);

  // increment the cf span
  impl->wip.cf.span += count;

#if !defined(NDEBUG) && 0
  fprintf(stderr,
          ">>> ( %5u , %5u , %5u )\n",
          dispatch->cf.span + impl->wip.cf.span,
          dispatch->cf.head,
          cf_ring->size);
#endif

  //
  // There will always be enough room in the TC ring so only its head
  // needs to be tracked.
  //
  struct spn_next * const tc_next = &impl->mapped.tc.next;

  //
  // The command's cohort id is the same for all commands
  //
  struct spn_cmd_fill cf;

  cf.cohort = dispatch->rc.span;

  //
  // append commands to the cf ring and dependent quads to the tc ring
  //
  while (true)
    {
      uint32_t const cf_idx = spn_ring_acquire_1(cf_ring);

      //
      // get the path
      //
      uint32_t const handle = paths->handle;

      impl->paths.extent[cf_idx] = handle;
      cf.path_h                  = handle;

      //
      // classify the transform
      //
      // if (w0==w1==0) then it's an affine matrix
      cf.transform_type = ((transforms->w0 == 0.0f) && (transforms->w1 == 0.0f))
                            ? SPN_CMD_FILL_TRANSFORM_TYPE_AFFINE
                            : SPN_CMD_FILL_TRANSFORM_TYPE_PROJECTIVE;

      //
      // if the weakref exists then reuse existing transform index
      //
      if (!spn_transform_weakrefs_get_index(transform_weakrefs, 0, &impl->epoch, &cf.transform))
        {
          uint32_t const t_idx = spn_next_acquire_2(tc_next);

          spn_transform_weakrefs_init(transform_weakrefs, 0, &impl->epoch, t_idx);

          cf.transform = t_idx;

          spn_rbi_transform_copy_lo(impl->mapped.tc.extent + t_idx + 0, transforms);
          spn_rbi_transform_copy_hi(impl->mapped.tc.extent + t_idx + 1, transforms);

          impl->wip.tc.span += 2;
        }

      //
      // if the weakref exists then reuse existing clip index
      //
      if (!spn_clip_weakrefs_get_index(clip_weakrefs, 0, &impl->epoch, &cf.clip))
        {
          uint32_t const c_idx = spn_next_acquire_1(tc_next);

          spn_clip_weakrefs_init(clip_weakrefs, 0, &impl->epoch, c_idx);

          cf.clip = c_idx;

          memcpy(impl->mapped.tc.extent + c_idx, clips, sizeof(*impl->mapped.tc.extent));

          impl->wip.tc.span += 1;
        }

      //
      // store the command to the ring
      //
      impl->mapped.cf.extent[cf_idx] = cf;

      //
      // no more paths?
      //
      if (--count == 0)
        break;

      //
      // otherwise, increment pointers
      //
      // FIXME(allanmac): this will be updated with an argument
      // "template" struct
      //
      paths++;

      if (transform_weakrefs != NULL)
        transform_weakrefs++;

      transforms++;

      if (clip_weakrefs != NULL)
        clip_weakrefs++;

      clips++;
    }

  return SPN_SUCCESS;
}

//
//
//

static spn_result_t
spn_rbi_release(struct spn_raster_builder_impl * const impl)
{
  //
  // launch any wip dispatch
  //
  spn_rbi_flush(impl);

  //
  // wait for all in-flight dispatches to complete
  //
  struct spn_ring * const   ring   = &impl->dispatches.ring;
  struct spn_device * const device = impl->device;

  while (!spn_ring_is_full(ring))
    {
      spn(device_wait(impl->device, __func__));
    }

  //
  // Note that we don't have to unmap before freeing
  //

  //
  // free copyback
  //
  spn_allocator_device_perm_free(&device->allocator.device.perm.hr_dw,
                                 &device->environment,
                                 &impl->vk.copyback.dbi,
                                 impl->vk.copyback.dm);
  //
  // free ring
  //
  if (spn_rbi_is_staged(impl->config))
    {
      spn_allocator_device_perm_free(&device->allocator.device.perm.drw,
                                     &device->environment,
                                     &impl->vk.rings.rc.d.dbi,
                                     impl->vk.rings.rc.d.dm);

      spn_allocator_device_perm_free(&device->allocator.device.perm.drw,
                                     &device->environment,
                                     &impl->vk.rings.tc.d.dbi,
                                     impl->vk.rings.tc.d.dm);

      spn_allocator_device_perm_free(&device->allocator.device.perm.drw,
                                     &device->environment,
                                     &impl->vk.rings.cf.d.dbi,
                                     impl->vk.rings.cf.d.dm);
    }

  spn_allocator_device_perm_free(&device->allocator.device.perm.hw_dr,
                                 &device->environment,
                                 &impl->vk.rings.rc.h.dbi,
                                 impl->vk.rings.rc.h.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.hw_dr,
                                 &device->environment,
                                 &impl->vk.rings.tc.h.dbi,
                                 impl->vk.rings.tc.h.dm);

  spn_allocator_device_perm_free(&device->allocator.device.perm.hw_dr,
                                 &device->environment,
                                 &impl->vk.rings.cf.h.dbi,
                                 impl->vk.rings.cf.h.dm);
  //
  // free host allocations
  //
  struct spn_allocator_host_perm * const perm = &impl->device->allocator.host.perm;

  spn_allocator_host_perm_free(perm, impl->rasters.extent);
  spn_allocator_host_perm_free(perm, impl->paths.extent);
  spn_allocator_host_perm_free(perm, impl->dispatches.extent);
  spn_allocator_host_perm_free(perm, impl->raster_builder);
  spn_allocator_host_perm_free(perm, impl);

  return SPN_SUCCESS;
}

//
//
//

spn_result_t
spn_raster_builder_impl_create(struct spn_device * const    device,
                               spn_raster_builder_t * const raster_builder)
{
  //
  // retain the context
  // spn_context_retain(context);
  //
  struct spn_allocator_host_perm * const perm = &device->allocator.host.perm;

  //
  // allocate impl
  //
  struct spn_raster_builder_impl * const impl =
    spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, sizeof(*impl));

  //
  // allocate raster builder
  //
  struct spn_raster_builder * const rb =
    spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, sizeof(*rb));

  // init impl and rb back-pointers
  *raster_builder      = rb;
  impl->raster_builder = rb;
  rb->impl             = impl;

  // save device
  impl->device = device;

  // save config
  struct spn_vk_target_config const * const config = spn_vk_get_config(device->instance);

  impl->config = config;

  //
  // init raster builder pfns
  //
  rb->begin   = spn_rbi_begin;
  rb->end     = spn_rbi_end;
  rb->release = spn_rbi_release;
  rb->flush   = spn_rbi_flush;
  rb->add     = spn_rbi_add;

  //
  // init refcount & state
  //
  rb->refcount = 1;

  SPN_ASSERT_STATE_INIT(SPN_RASTER_BUILDER_STATE_READY, rb);

  //
  // Allocate rings
  //

  //
  // CF: 1 ring entry per command
  //
  spn_ring_init(&impl->mapped.cf.ring, config->raster_builder.size.ring);

  //
  // TC: 1 transform + 1 clip = 3 quads
  //
  // NOTE(allanmac): one additional quad is required because transforms
  // require 2 consecutive quads and the worst case would be a full ring
  // of commands each with a transform and clip.
  //
  uint32_t const tc_ring_size = config->raster_builder.size.ring * 3 + 1;

  spn_next_init(&impl->mapped.tc.next, tc_ring_size);

  //
  // RC:  worst case 1:1 (cmds:rasters)
  //
  spn_next_init(&impl->mapped.rc.next, config->raster_builder.size.ring);

  //
  // allocate and map CF
  //
  VkDeviceSize const cf_size = sizeof(*impl->mapped.cf.extent) * config->raster_builder.size.ring;

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.hw_dr,
                                  &device->environment,
                                  cf_size,
                                  NULL,
                                  &impl->vk.rings.cf.h.dbi,
                                  &impl->vk.rings.cf.h.dm);

  vk(MapMemory(device->environment.d,
               impl->vk.rings.cf.h.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.cf.extent));

  //
  // allocate and map TC
  //
  VkDeviceSize const tc_size = sizeof(*impl->mapped.tc.extent) * tc_ring_size;

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.hw_dr,
                                  &device->environment,
                                  tc_size,
                                  NULL,
                                  &impl->vk.rings.tc.h.dbi,
                                  &impl->vk.rings.tc.h.dm);

  vk(MapMemory(device->environment.d,
               impl->vk.rings.tc.h.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.tc.extent));

  //
  // allocate and map RC
  //
  VkDeviceSize const rc_size = sizeof(*impl->mapped.rc.extent) * config->raster_builder.size.ring;

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.hw_dr,
                                  &device->environment,
                                  rc_size,
                                  NULL,
                                  &impl->vk.rings.rc.h.dbi,
                                  &impl->vk.rings.rc.h.dm);

  vk(MapMemory(device->environment.d,
               impl->vk.rings.rc.h.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.rc.extent));

  //
  // discrete GPU?
  //
  if (spn_rbi_is_staged(impl->config))
    {
      spn_allocator_device_perm_alloc(&device->allocator.device.perm.drw,
                                      &device->environment,
                                      cf_size,
                                      NULL,
                                      &impl->vk.rings.cf.d.dbi,
                                      &impl->vk.rings.cf.d.dm);

      spn_allocator_device_perm_alloc(&device->allocator.device.perm.drw,
                                      &device->environment,
                                      tc_size,
                                      NULL,
                                      &impl->vk.rings.tc.d.dbi,
                                      &impl->vk.rings.tc.d.dm);

      spn_allocator_device_perm_alloc(&device->allocator.device.perm.drw,
                                      &device->environment,
                                      rc_size,
                                      NULL,
                                      &impl->vk.rings.rc.d.dbi,
                                      &impl->vk.rings.rc.d.dm);
    }
  else
    {
      impl->vk.rings.cf.d.dbi = impl->vk.rings.cf.h.dbi;
      impl->vk.rings.cf.d.dm  = impl->vk.rings.cf.h.dm;

      impl->vk.rings.tc.d.dbi = impl->vk.rings.tc.h.dbi;
      impl->vk.rings.tc.d.dm  = impl->vk.rings.tc.h.dm;

      impl->vk.rings.rc.d.dbi = impl->vk.rings.rc.h.dbi;
      impl->vk.rings.rc.d.dm  = impl->vk.rings.rc.h.dm;
    }

  //
  // allocate and map copyback
  //
  uint32_t const max_in_flight = config->raster_builder.size.dispatches;
  size_t const   copyback_size = max_in_flight * sizeof(*impl->mapped.cb.extent);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.hr_dw,
                                  &device->environment,
                                  copyback_size,
                                  NULL,
                                  &impl->vk.copyback.dbi,
                                  &impl->vk.copyback.dm);

  vk(MapMemory(device->environment.d,
               impl->vk.copyback.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.cb.extent));

  //
  // allocate release resources
  //
  size_t const dispatches_size = sizeof(*impl->dispatches.extent) * max_in_flight;
  size_t const paths_size      = sizeof(*impl->paths.extent) * config->raster_builder.size.ring;
  size_t const rasters_size    = sizeof(*impl->rasters.extent) * config->raster_builder.size.ring;

  impl->dispatches.extent = spn_allocator_host_perm_alloc(perm,  //
                                                          SPN_MEM_FLAGS_READ_WRITE,
                                                          dispatches_size);

  impl->paths.extent = spn_allocator_host_perm_alloc(perm,  //
                                                     SPN_MEM_FLAGS_READ_WRITE,
                                                     paths_size);

  impl->rasters.extent = spn_allocator_host_perm_alloc(perm,  //
                                                       SPN_MEM_FLAGS_READ_WRITE,
                                                       rasters_size);

  spn_ring_init(&impl->dispatches.ring, max_in_flight);

  spn_rbi_wip_init(impl);

  spn_rbi_dispatch_init(impl, impl->dispatches.extent);

  spn_weakref_epoch_init(&impl->epoch);

  return SPN_SUCCESS;
}

//
//
//
