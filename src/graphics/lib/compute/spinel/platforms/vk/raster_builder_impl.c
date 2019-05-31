// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "raster_builder_impl.h"

#include <memory.h>

#include "block_pool.h"
#include "common/macros.h"
#include "common/vk/vk_assert.h"
#include "common/vk/vk_barrier.h"
#include "device.h"
#include "handle_pool.h"
#include "path_builder.h"
#include "queue_pool.h"
#include "ring.h"
#include "semaphore_pool.h"
#include "spn_vk_target.h"
#include "weakref.h"

//
// The raster builder prepares fill commands, transforms and clips for
// the rasterization sub-pipeline.
//
// A simplifying assumption is that the maximum length of a single
// raster can't be larger than what fits in the raster builder
// ring.
//
// This would be a very long raster and a legitimate size limitation.
//
// If a raster is too long then the raster builder instance is lost.
//
// Note that this restriction can be removed with added complexity to
// the builder and shader.
//
// The general strategy that this particular Vulkan implementation
// uses is to allocate a large "HOST_COHERENT" buffer for the ring.
//
// Note that the maximum number of "in-flight" rasterization
// sub-pipelines conveniently determined by the size of the fence
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

//
// Note that the fill command can reduce its transform and clip fields
// to 13-16 bits and fit into 3 dwords but... it's easier to use a
// uint4 with GPUs.
//
// FIXME: A non-affine transformation elevates a Bezier to a rational.
// For this reason, we need to indicate with a bit flag if the
// transform matrix has non-zero {w0,w1} elements.  The bit can be
// part of the transform dword or stuffed into the unused cohort bits.
//

struct spn_cmd_fill
{
  uint32_t path_h;       // host id
  uint32_t na0 : 16;     // unused
  uint32_t cohort : 16;  // cohort is limited to 13 bits
  uint32_t transform;    // index of first quad of transform
  uint32_t clip;         // index of clip
};

STATIC_ASSERT_MACRO_1(sizeof(struct spn_cmd_fill) == sizeof(uint32_t[4]));

//
// There are always as many dispatch records as there are fences in
// the fence pool.  This simplifies reasoning about concurrency.
//

struct spn_rbi_dispatch
{
  struct
  {
    uint32_t span;
    uint32_t head;
  } cf;  // fills and paths are 1:1

  struct
  {
    uint32_t span;
    uint32_t head;
  } tc;  // transform quads and clips

  struct
  {
    uint32_t span;
    uint32_t head;
  } rc;  // rasters in cohort

  bool unreleased;
};

//
// The host-side rings share a single host-coherent buffer:
//
//   |<--cmds(uvec4)-->|<--transforms & clips(vec4)-->|<--rasters(uint)-->|
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

struct spn_rbi_vk
{
  struct
  {
    struct
    {
      VkDescriptorBufferInfo dbi;
      VkDeviceMemory         dm;
    } h;

    struct
    {
      VkDescriptorBufferInfo dbi;
      VkDeviceMemory         dm;
    } d;

    VkDescriptorBufferInfo cf;
    VkDescriptorBufferInfo tc;
    VkDescriptorBufferInfo rc;
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
  // we use simpler accounting for tcs.
  //
  struct
  {
    struct
    {
      struct spn_cmd_fill * extent;  // uint32_t[4]
      struct spn_ring       ring;
    } cf;  // fill commands

    struct
    {
      float (*extent)[4];  // float[4]
      struct spn_next next;
    } tc;  // transforms & clips

    struct
    {
      spn_raster_t *  extent;  // uint32_t
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
      uint32_t head;  // get rid of head
      uint32_t span;
    } cf;  // fills

    struct
    {
      uint32_t head;  // get rid of head
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
    struct spn_rbi_dispatch * extent;
    struct spn_ring           ring;
  } dispatches;

  struct
  {
    spn_path_t *    extent;
    struct spn_next next;
  } paths;

  struct
  {
    spn_raster_t *  extent;
    struct spn_next next;
  } rasters;
};

//
//
//

static spn_result
spn_rbi_lost_begin(struct spn_raster_builder_impl * const impl)
{
  return SPN_ERROR_RASTER_BUILDER_LOST;
}

static spn_result
spn_rbi_lost_end(struct spn_raster_builder_impl * const impl, spn_raster_t * const raster)
{
  *raster = UINT32_MAX;  // FIXME -- SPN_TYPED_HANDLE_INVALID

  return SPN_ERROR_RASTER_BUILDER_LOST;
}

static spn_result
spn_rbi_release(struct spn_raster_builder_impl * const impl);

static spn_result
spn_rbi_lost_release(struct spn_raster_builder_impl * const impl)
{
  //
  // FIXME -- releasing a lost path builder might eventually require a
  // specialized function.  For now, just call the default release.
  //
  return spn_rbi_release(impl);
}

static spn_result
spn_rbi_lost_flush(struct spn_raster_builder_impl * const impl)
{
  return SPN_ERROR_RASTER_BUILDER_LOST;
}

static spn_result
spn_rbi_lost_fill(struct spn_raster_builder_impl * const impl,
                  spn_path_t * const                     paths,
                  spn_transform_weakref_t * const        transform_weakrefs,
                  float const (*const transforms)[8],
                  spn_clip_weakref_t * const clip_weakrefs,
                  float const (*const clips)[4],
                  uint32_t count)
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
  rb->fill    = spn_rbi_lost_fill;
}

//
// Append path to path release extent -- note that this resource is
// implicitly "clocked" by the mapped.ring.
//

static void
spn_rbi_path_append(struct spn_raster_builder_impl * const impl, spn_path_t const path)
{
  uint32_t const idx = spn_next_acquire_1(&impl->paths.next);

  impl->paths.extent[idx] = path;
}

static void
spn_rbi_raster_append(struct spn_raster_builder_impl * const impl, spn_raster_t const raster)
{
  uint32_t const idx = spn_next_acquire_1(&impl->rasters.next);

  impl->rasters.extent[idx] = raster;
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
  dispatch->cf.head = impl->wip.cf.head;

  dispatch->tc.span = 0;
  dispatch->tc.head = impl->wip.tc.head;

  dispatch->rc.span = 0;
  dispatch->rc.head = impl->rasters.next.head;

  dispatch->unreleased = false;
}

static void
spn_rbi_dispatch_drop(struct spn_raster_builder_impl * const impl)
{
  struct spn_ring * const ring = &impl->dispatches.ring;

  spn_ring_drop_1(ring);

  while (spn_ring_is_empty(ring))
    {
      spn_device_wait(impl->device);
    }

  struct spn_rbi_dispatch * const dispatch = spn_rbi_dispatch_idx(impl, ring->head);

  spn_rbi_dispatch_init(impl, dispatch);
}

static void
spn_rbi_dispatch_append(struct spn_raster_builder_impl * const impl, spn_raster_t const raster)
{
  spn_rbi_raster_append(impl, raster);

  struct spn_rbi_dispatch * const dispatch = spn_rbi_dispatch_head(impl);

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
// PHASE 2
//

struct spn_rbi_complete_payload_2
{
  struct spn_raster_builder_impl * impl;

  struct
  {
    VkSemaphore sort;
  } semaphore;

  struct
  {
    struct spn_vk_ds_rasterize_post_t rp;
  } ds;

  struct
  {
    struct
    {
      VkDeviceSize    offset;
      spn_subbuf_id_t ttrks;
    } rp;
  } temp;

  uint32_t dispatch_idx;
};

//
// PHASE 1
//

struct spn_rbi_complete_payload_1
{
  struct spn_rbi_complete_payload_2 p_2;

  struct
  {
    VkSemaphore copy;
  } semaphore;

  struct
  {
    struct spn_vk_ds_rasterize_t r;
  } ds;

  struct
  {
    struct
    {
      spn_subbuf_id_t fill_scan;
      spn_subbuf_id_t rast_cmds;
    } r;
  } temp;
};

//
//
//

static void
spn_rbi_complete_p_2(void * pfn_payload)
{
  //
  // FENCE_POOL INVARIANT:
  //
  // COMPLETION ROUTINE MUST MAKE LOCAL COPIES OF PAYLOAD BEFORE ANY
  // POTENTIAL INVOCATION OF SPN_DEVICE_YIELD/WAIT/DRAIN()
  //
  // The safest approach is to create a copy of payload struct on the
  // stack if you don't understand where the wait()'s might occur.
  //
  struct spn_rbi_complete_payload_2 const * const p_2      = pfn_payload;
  struct spn_raster_builder_impl * const          impl     = p_2->impl;
  struct spn_device * const                       device   = impl->device;
  struct spn_vk * const                           instance = device->target;

  // release the copy semaphore
  spn_device_semaphore_pool_release(device, p_2->semaphore.sort);

  // release the rasterize ds -- will never wait()
  spn_vk_ds_release_rasterize_post(instance, p_2->ds.rp);

  // release the rasterize post temp buffer -- will never wait()
  spn_allocator_device_temp_free(&device->allocator.device.temp.local, p_2->temp.rp.ttrks);

  //
  // release rasters
  //
  uint32_t const            dispatch_idx = p_2->dispatch_idx;
  struct spn_rbi_dispatch * dispatch     = spn_rbi_dispatch_idx(impl, dispatch_idx);

  spn_device_handle_pool_release_ring_d_rasters(device,
                                                impl->rasters.extent,
                                                impl->rasters.next.size,
                                                dispatch->rc.span,
                                                dispatch->rc.head);
  //
  // If the dispatch is the tail of the ring then try to release as
  // many dispatch records as possible...
  //
  // Note that kernels can complete in any order so the release
  // records need to add to the mapped.ring.tail in order.
  //
  if (spn_ring_is_tail(&impl->dispatches.ring, dispatch_idx))
    {
      do
        {
          dispatch->unreleased = false;

          spn_ring_release_n(&impl->mapped.cf.ring, dispatch->cf.span);
          spn_ring_release_n(&impl->dispatches.ring, 1);

          dispatch = spn_rbi_dispatch_tail(impl);
        }
      while (dispatch->unreleased);
    }
  else
    {
      dispatch->unreleased = true;
    }
}

//
//
//

static void
spn_rbi_complete_p_1(void * pfn_payload)
{
  //
  // FENCE_POOL INVARIANT:
  //
  // COMPLETION ROUTINE MUST MAKE LOCAL COPIES OF PAYLOAD BEFORE ANY
  // POTENTIAL INVOCATION OF SPN_DEVICE_YIELD/WAIT/DRAIN()
  //
  // The safest approach is to create a copy of payload struct on the
  // stack if you don't understand where the wait()'s might occur.
  //
  struct spn_rbi_complete_payload_1 const * const p_1      = pfn_payload;
  struct spn_raster_builder_impl * const          impl     = p_1->p_2.impl;
  struct spn_device * const                       device   = impl->device;
  struct spn_vk * const                           instance = device->target;

  //
  // Release resources
  //

  // release the copy semaphore
  spn_device_semaphore_pool_release(device, p_1->semaphore.copy);

  // release the rasterize ds -- will never wait()
  spn_vk_ds_release_rasterize(instance, p_1->ds.r);

  // release the rasterize temp buffers -- will never wait()
  spn_allocator_device_temp_free(&device->allocator.device.temp.local, p_1->temp.r.fill_scan);

  spn_allocator_device_temp_free(&device->allocator.device.temp.local, p_1->temp.r.rast_cmds);
  //
  // Command Buffer 2
  // ----------------
  //
  // The second command buffer post-processes the rasterized paths.
  //
  //   2.1) MERGE TTRK KEYS
  //
  //   2.2) SEGMENT_TTRK
  //
  //   2.3) RASTERS_ALLOC
  //
  //   2.4) RASTERS_PREFIX
  //

  //
  // Make a copy of p_2
  //
  struct spn_rbi_complete_payload_2 p_2 = p_1->p_2;

  // acquire another cb
  VkCommandBuffer cb_3 = spn_device_cb_acquire_begin(device);

  //
  // DS: BLOCK_POOL
  //
  // bind the global BLOCK_POOL descriptor set
  spn_vk_ds_bind_segment_ttrk_block_pool(instance, cb_3, spn_device_block_pool_get_ds(device));

  //
  // DS: RASTERIZE_POST
  //
  spn_vk_ds_bind_segment_ttrk_rasterize_post(instance, cb_3, p_2.ds.rp);

  //
  // 2.1) MERGE TTRK KEYS
  //
  //
  // Launch HotSort merging phase dependent on the sort semaphore
  //
#if 0
  hs_vk_merge(cb_3,
              p_2.temp.rp.offset + SPN_VK_TARGET_BUFFER_OFFSETOF(rasterize_post,ttrks,ttrks_keys),
              impl->mapped.cb[p2.dispatch_idx], // copyback key count
              1,
              &p_2.semaphore.sort,
              0,
              NULL);
#endif

  vk_barrier_compute_w_to_compute_r(cb_3);

  //
  //   2.2) SEGMENT_TTRK
  //
  //   2.3) RASTERS_ALLOC
  //
  //   2.4) RASTERS_PREFIX
  //
  struct spn_rbi_dispatch * dispatch = spn_rbi_dispatch_idx(impl, p_2.dispatch_idx);

  ////////////////////////////////////////////////////////////////
  //
  // SHADER: SEGMENT_TTRK
  //
  ////////////////////////////////////////////////////////////////

  // bind the pipeline
  spn_vk_p_bind_segment_ttrk(instance, cb_3);

  // dispatch one workgroup per fill command
  vkCmdDispatch(cb_3, 99999, 1, 1);  // FIXME -- calculate slab count

  // compute barrier
  vk_barrier_compute_w_to_compute_r(cb_3);

  ////////////////////////////////////////////////////////////////
  //
  // SHADER: RASTERS_ALLOC
  //
  ////////////////////////////////////////////////////////////////

  struct spn_vk_push_rasters_alloc const push_rasters_alloc = {
    .bp_mask   = spn_device_block_pool_get_mask(device),
    .cmd_count = dispatch->rc.span};

  // bind the push constants
  spn_vk_p_push_rasters_alloc(instance, cb_3, &push_rasters_alloc);

  // bind the pipeline
  spn_vk_p_bind_rasters_alloc(instance, cb_3);

  // dispatch one subgroup (workgroup) per raster
  vkCmdDispatch(cb_3, dispatch->rc.span, 1, 1);

  // compute barrier
  vk_barrier_compute_w_to_compute_r(cb_3);

  ////////////////////////////////////////////////////////////////
  //
  // SHADER: RASTERS_PREFIX
  //
  ////////////////////////////////////////////////////////////////

  // push constants remain the same

  // bind the pipeline
  spn_vk_p_bind_rasters_prefix(instance, cb_3);

  // dispatch one subgroup (workgroup) per raster
  vkCmdDispatch(cb_3, dispatch->rc.span, 1, 1);

  //
  // wait for sort to complete before executing
  //
  VkFence const fence =
    spn_device_cb_end_fence_acquire(device, cb_3, spn_rbi_complete_p_2, &p_2, sizeof(p_2));
  // boilerplate submit
  struct VkSubmitInfo const si = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = NULL,
    .waitSemaphoreCount   = 1,
    .pWaitSemaphores      = &p_2.semaphore.sort,
    .pWaitDstStageMask    = &(VkPipelineStageFlags){VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb_3,
    .signalSemaphoreCount = 0,
    .pSignalSemaphores    = NULL};

  vk(QueueSubmit(spn_device_queue_next(device), 1, &si, fence));

  //
  // Release paths after submitting the phase 2 command buffer to
  // reduce latency since this might result in many PATHS_RELEASE
  // shaders being launched.
  //
  spn_device_handle_pool_release_ring_d_paths(device,
                                              impl->paths.extent,
                                              impl->paths.next.size,
                                              dispatch->cf.span,
                                              dispatch->cf.head);
}

//
//
//

static spn_result
spn_rbi_flush(struct spn_raster_builder_impl * const impl)
{
  struct spn_rbi_dispatch * const dispatch = spn_rbi_dispatch_head(impl);

  // anything to launch?
  if (spn_rbi_is_wip_dispatch_empty(dispatch))
    return SPN_SUCCESS;

  //
  // We're go for launch...
  //
  // The rasterization sub-pipeline is fairly intricate and launches
  // two command buffers.
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
  //   1.2) FILLS_EXPAND
  //
  //        Expand the fill command into rasterization commands and
  //        store them to a temporary buffer:
  //
  //          |<lines><quads><cubics><rat_quads><rat_cubics>|
  //
  //   1.3) FILLS_DISPATCH
  //
  //        Take the atomically updated count of rasterization commands
  //        and initialize a workgroup triple for
  //        vkCmdDispatchIndirect().
  //
  //   1.4) RASTERIZE_LINES/QUADS/CUBICS/RAT_QUADS/RAT_CUBICS
  //
  //        For each path type, indirectly dispatch a rasterizer.
  //
  //   1.5) COPY TTRK KEYS COUNT TO HOST
  //
  //   1.6) HOTSORT INDIRECT SORT TTRKS
  //
  //        Sorting can be performed indirectly.  Merging requires
  //        knowing the key count.
  //
  //
  // Command Buffer 2
  // ----------------
  //
  // The second command buffer post-processes the rasterized paths.
  //
  //   2.1) MERGE TTRK KEYS
  //
  //   2.2) SEGMENT_TTRK
  //
  //   2.3) RASTERS_ALLOC
  //
  //   2.4) RASTERS_PREFIX
  //
  struct spn_device * const                 device   = impl->device;
  struct spn_vk * const                     instance = device->target;
  struct spn_vk_target_config const * const config   = spn_vk_get_config(device->target);

  //
  // COMMAND BUFFER 1
  //

  //
  // callback state
  //
  struct spn_rbi_complete_payload_1 p_1 = {
    .p_2 = {.impl = impl, .dispatch_idx = impl->dispatches.ring.head}};

  //
  VkCommandBuffer cb_1 = spn_device_cb_acquire_begin(device);

  //
  // DS: BLOCK_POOL
  //
  // bind the global BLOCK_POOL descriptor set
  spn_vk_ds_bind_fills_scan_block_pool(instance, cb_1, spn_device_block_pool_get_ds(device));

  //
  // DS: RASTERIZE
  //
  spn_vk_ds_acquire_rasterize(instance, device, &p_1.ds.r);

  // dbi: fill_cmds
  *spn_vk_ds_get_rasterize_fill_cmds(instance, p_1.ds.r) = impl->vk.rings.cf;

  // dbi: fill_quads
  *spn_vk_ds_get_rasterize_fill_quads(instance, p_1.ds.r) = impl->vk.rings.tc;

  // dbi: fill_scan -- allocate a temporary buffer
  VkDescriptorBufferInfo * const dbi_fill_scan =
    spn_vk_ds_get_rasterize_fill_scan(instance, p_1.ds.r);

  spn_allocator_device_temp_alloc(
    &device->allocator.device.temp.local,
    device,
    spn_device_wait,
    SPN_VK_TARGET_BUFFER_OFFSETOF(rasterize, fill_scan, fill_scan_prefix) +
      dispatch->cf.span * sizeof(SPN_TYPE_UVEC4),
    &p_1.temp.r.fill_scan,
    dbi_fill_scan);

  // dbi: rast_cmds -- allocate a temporary buffer
  spn_allocator_device_temp_alloc(&device->allocator.device.temp.local,
                                  device,
                                  spn_device_wait,
                                  SPN_VK_TARGET_BUFFER_OFFSETOF(rasterize, rast_cmds, rast_cmds) +
                                    config->raster_builder.size.cmds * sizeof(SPN_TYPE_UVEC4),
                                  &p_1.temp.r.rast_cmds,
                                  spn_vk_ds_get_rasterize_rast_cmds(instance, p_1.ds.r));

  // update rasterize ds
  spn_vk_ds_update_rasterize(instance, device->environment, p_1.ds.r);

  // bind rasterize ds
  spn_vk_ds_bind_fills_scan_rasterize(instance, cb_1, p_1.ds.r);

  //
  // DS: RASTERIZE_POST
  //
  spn_vk_ds_acquire_rasterize_post(instance, device, &p_1.p_2.ds.rp);

  // dbi: ttrks -- allocate a temporary buffer
  VkDescriptorBufferInfo * const dbi_ttrks =
    spn_vk_ds_get_rasterize_post_ttrks(instance, p_1.p_2.ds.rp);

  spn_allocator_device_temp_alloc(&device->allocator.device.temp.local,
                                  device,
                                  spn_device_wait,
                                  SPN_VK_TARGET_BUFFER_OFFSETOF(rasterize_post, ttrks, ttrks_keys) +
                                    config->raster_builder.size.ttrks * sizeof(SPN_TYPE_UVEC2),
                                  &p_1.p_2.temp.rp.ttrks,
                                  dbi_ttrks);

  p_1.p_2.temp.rp.offset = dbi_ttrks->offset;

  // update rasterize_post ds
  spn_vk_ds_update_rasterize_post(instance, device->environment, p_1.p_2.ds.rp);

  // bind rasterize_post ds
  spn_vk_ds_bind_rasterize_line_rasterize_post(instance, cb_1, p_1.p_2.ds.rp);

  ////////////////////////////////////////////////////////////////
  //
  // SHADER: FILLS_SCAN
  //
  ////////////////////////////////////////////////////////////////

  struct spn_vk_push_fills_scan const push_fills_scan = {
    .bp_mask   = spn_device_block_pool_get_mask(device),
    .cmd_count = dispatch->cf.span};

  // bind the push constants
  spn_vk_p_push_fills_scan(instance, cb_1, &push_fills_scan);

  // bind the pipeline
  spn_vk_p_bind_fills_scan(instance, cb_1);

  // dispatch one workgroup per fill command
  vkCmdDispatch(cb_1, dispatch->cf.span, 1, 1);

  // compute barrier
  vk_barrier_compute_w_to_compute_r(cb_1);

  ////////////////////////////////////////////////////////////////
  //
  // SHADER: FILLS_EXPAND
  //
  ////////////////////////////////////////////////////////////////

  // no need to set up push constants since they're identical to FILLS_SCAN

  // bind the pipeline
  spn_vk_p_bind_fills_expand(instance, cb_1);

  // dispatch one workgroup per fill command
  vkCmdDispatch(cb_1, dispatch->cf.span, 1, 1);

  // compute barrier
  vk_barrier_compute_w_to_compute_r(cb_1);

  ////////////////////////////////////////////////////////////////
  //
  // SHADER: FILLS_DISPATCH
  //
  ////////////////////////////////////////////////////////////////

  // no push constants

  // bind the pipeline
  spn_vk_p_bind_fills_dispatch(instance, cb_1);

  // dispatch one workgroup per fill command
  vkCmdDispatch(cb_1, 1, 1, 1);

  // compute barrier
  vk_barrier_compute_w_to_compute_r(cb_1);

  ////////////////////////////////////////////////////////////////
  //
  // SHADERS: RASTERIZE_[LINES|QUADS|CUBICS|RAT_QUADS|RAT_CUBICS]
  //
  ////////////////////////////////////////////////////////////////

#define SPN_VK_TARGET_P_BIND_RASTERIZE_NAME(_p) spn_vk_p_bind_rasterize_##_p

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n)                                            \
  SPN_VK_TARGET_P_BIND_RASTERIZE_NAME(_p)(instance, cb_1);                                         \
  vkCmdDispatchIndirect(cb_1, dbi_fill_scan->buffer, sizeof(SPN_TYPE_UVEC4) * _i);

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()

  ////////////////////////////////////////////////////////////////
  //
  // RASTERIZATION COMPLETE -- copyback ttrk count
  //
  ////////////////////////////////////////////////////////////////

  vk_barrier_compute_w_to_transfer_r(cb_1);

  //
  // COPYBACK
  //
  VkBufferCopy const bc = {
    .srcOffset =
      p_1.p_2.temp.rp.offset + SPN_VK_TARGET_BUFFER_OFFSETOF(rasterize_post, ttrks, ttrks_count),
    .dstOffset = sizeof(*impl->mapped.cb.extent) * p_1.p_2.dispatch_idx,
    .size      = sizeof(*impl->mapped.cb.extent)};

  vkCmdCopyBuffer(cb_1, dbi_ttrks->buffer, impl->vk.copyback.dbi.buffer, 1, &bc);

  //
  // Acquire post-copyback and post-sort semaphores
  //
  p_1.semaphore.copy     = spn_device_semaphore_pool_acquire(device);
  p_1.p_2.semaphore.sort = spn_device_semaphore_pool_acquire(device);

  //
  // submit the command buffer
  //

  {
    VkFence const fence =
      spn_device_cb_end_fence_acquire(device, cb_1, spn_rbi_complete_p_1, &p_1, sizeof(p_1));
    // boilerplate submit
    struct VkSubmitInfo const si = {.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                    .pNext                = NULL,
                                    .waitSemaphoreCount   = 0,
                                    .pWaitSemaphores      = NULL,
                                    .pWaitDstStageMask    = NULL,
                                    .commandBufferCount   = 1,
                                    .pCommandBuffers      = &cb_1,
                                    .signalSemaphoreCount = 1,
                                    .pSignalSemaphores    = &p_1.semaphore.copy};

    vk(QueueSubmit(spn_device_queue_next(device), 1, &si, fence));
  }

#if 0
  //
  // Launch HotSort indirect sorting phase as soon as the copy completes
  //
  {
    VkCommandBuffer cb_2 = spn_device_cb_acquire_begin(device);

    hs_vk_sort_indirect(cb_2,
                        dbi_ttrks->buffer,
                        p_1.p_2.temp.rp.offset + SPN_VK_TARGET_BUFFER_OFFSETOF(rasterize_post,ttrks,ttrks_keys),
                        ttrks_count_offset);

    VkFence const fence = spn_device_cb_end_fence_acquire(device,
                                                          cb_2,
                                                          NULL,
                                                          NULL,
                                                          0UL);
    // boilerplate submit
    struct VkSubmitInfo const si = {
      .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext                = NULL,
      .waitSemaphoreCount   = 1,
      .pWaitSemaphores      = &p_1.semaphore.copy,
      .pWaitDstStageMask    = &(VkPipelineStageFlags){VK_PIPELINE_STAGE_TRANSFER_BIT},
      .commandBufferCount   = 1,
      .pCommandBuffers      = &cb_2,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores    = &p_1.semaphore.sort
    };

    vk(QueueSubmit(spn_device_queue_next(device),1,&si,fence));
  }
#endif

  //
  // the current dispatch is now "in flight" so drop it and try to
  // acquire and initialize the next
  //
  spn_rbi_dispatch_drop(impl);

  return SPN_SUCCESS;
}

//
// We record where the *next* work-in-progress path will start in the
// ring along with its rolling counter.
//

static void
spn_rbi_wip_init(struct spn_raster_builder_impl * const impl)
{
  impl->wip.cf.head = impl->mapped.cf.ring.head;
  impl->wip.cf.span = 0;

  impl->wip.tc.head = impl->mapped.tc.next.head;
  impl->wip.tc.span = 0;
}

//
//
//

static spn_result
spn_rbi_begin(struct spn_raster_builder_impl * const impl)
{
  // nothing to do here

  return SPN_SUCCESS;
}

//
//
//

static spn_result
spn_rbi_end(struct spn_raster_builder_impl * const impl, spn_raster_t * const raster)
{
  // acquire raster host id
  spn_device_handle_pool_acquire(impl->device, raster);

  // update head dispatch record
  spn_rbi_dispatch_append(impl, *raster);

  // add guard bit
  *raster |= SPN_TYPED_HANDLE_TYPE_RASTER;

#if 0
  if (spn_rbi_dispatch_head(impl)->blocks.span >= impl->config.raster_builder.size.eager) {
    spn_rbi_flush(impl);
  }
#endif

  spn_rbi_wip_init(impl);

  return SPN_SUCCESS;
}

//
//
//

static spn_result
spn_rbi_fill(struct spn_raster_builder_impl * const impl,
             spn_path_t * const                     paths,
             spn_transform_weakref_t * const        transform_weakrefs,
             float const (*const transforms)[8],
             spn_clip_weakref_t * const clip_weakrefs,
             float const (*const clips)[4],
             uint32_t count)
{
  if (count == 0)
    return SPN_SUCCESS;

  struct spn_ring * const         cf_ring  = &impl->mapped.cf.ring;
  struct spn_rbi_dispatch * const dispatch = spn_rbi_dispatch_head(impl);

  // if no more slots in command ring
  if (cf_ring->rem < count)
    {
      // If dispatch is empty and the work-in-progress is going to
      // exceed the size of the ring then this is a fatal error. At
      // this point, we can kill the path builder instead of the
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
          spn_device_wait(impl->device);
        }
      while (cf_ring->rem < count);
    }

  // validate and retain the paths
  spn_device_handle_pool_validate_retain_h_paths(impl->device, paths, count);

  // increment the cf span
  impl->wip.cf.span += count;

  struct spn_cmd_fill cf;

  // initialize the fill command
  cf.cohort = dispatch->rc.span;

  struct spn_next * const   tc_next = &impl->mapped.tc.next;
  spn_weakref_epoch_t const epoch   = impl->epoch;

  do
    {
      uint32_t cf_rem = MIN_MACRO(uint32_t, count, spn_ring_rem_nowrap(cf_ring));

      spn_ring_drop_n(cf_ring, cf_rem);

      count -= cf_rem;

      struct spn_cmd_fill * cf_extent = impl->mapped.cf.extent + cf_ring->head;

      for (uint32_t ii = 0; ii < cf_rem; ii++)
        {
          spn_handle_t const path = SPN_TYPED_HANDLE_TO_HANDLE(paths[ii]);

          spn_rbi_path_append(impl, path);

          cf.path_h = path;

          spn_transform_weakref_t * const tw = transform_weakrefs + ii;

          if (!spn_weakref_get_index(tw, epoch, &cf.transform))
            {
              uint32_t const t_idx0 = spn_next_acquire_1(tc_next);

              spn_weakref_update(tw, epoch, t_idx0);

              cf.transform = t_idx0;

              float const(*t)[8] = transforms + ii;

              memcpy(impl->mapped.tc.extent + t_idx0,
                     t + 0,  // lo quad
                     sizeof(*impl->mapped.tc.extent));

              uint32_t const t_idx1 = spn_next_acquire_1(tc_next);

              memcpy(impl->mapped.tc.extent + t_idx1,
                     t + 4,  // hi quad
                     sizeof(*impl->mapped.tc.extent));

              impl->wip.tc.span += 2;
            }

          spn_transform_weakref_t * const cw = clip_weakrefs + ii;

          if (!spn_weakref_get_index(cw, epoch, &cf.clip))
            {
              uint32_t const c_idx = spn_next_acquire_1(tc_next);

              spn_weakref_update(cw, epoch, c_idx);

              cf.clip = c_idx;

              memcpy(impl->mapped.tc.extent + c_idx, clips + ii, sizeof(*impl->mapped.tc.extent));

              impl->wip.tc.span += 1;
            }

          // store the command to the ring
          cf_extent[ii] = cf;
        }
    }
  while (count > 0);

  return SPN_SUCCESS;
}

//
//
//

static spn_result
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
      spn_device_wait(impl->device);
    }

  //
  // Note that we don't have to unmap before freeing
  //

  //
  // free copyback
  //
  spn_allocator_device_perm_free(&device->allocator.device.perm.copyback,
                                 device->environment,
                                 &impl->vk.copyback.dbi,
                                 impl->vk.copyback.dm);
  //
  // free ring
  //
  struct spn_vk_target_config const * const config = spn_vk_get_config(device->target);

  if (config->raster_builder.vk.rings.d != 0)
    {
      spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                     device->environment,
                                     &impl->vk.rings.d.dbi,
                                     impl->vk.rings.d.dm);
    }

  spn_allocator_device_perm_free(&device->allocator.device.perm.coherent,
                                 device->environment,
                                 &impl->vk.rings.h.dbi,
                                 impl->vk.rings.h.dm);
  //
  // free host allocations
  //
  struct spn_allocator_host_perm * const perm = &impl->device->allocator.host.perm;

  spn_allocator_host_perm_free(perm, impl->dispatches.extent);
  spn_allocator_host_perm_free(perm, impl->raster_builder);
  spn_allocator_host_perm_free(perm, impl);

  return SPN_SUCCESS;
}

//
//
//

spn_result
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
  struct spn_vk_target_config const * const config = spn_vk_get_config(device->target);

  impl->config = config;

  //
  // init raster builder pfns
  //
  rb->begin   = spn_rbi_begin;
  rb->end     = spn_rbi_end;
  rb->release = spn_rbi_release;
  rb->flush   = spn_rbi_flush;
  rb->fill    = spn_rbi_fill;

  //
  // init refcount & state
  //
  rb->refcount = 1;

  SPN_ASSERT_STATE_INIT(rb, SPN_RASTER_BUILDER_STATE_READY);

  //
  // init ring/next/next
  //
  spn_ring_init(&impl->mapped.cf.ring,
                config->raster_builder.size.ring);  // number of commands
  spn_next_init(&impl->mapped.rc.next,
                config->raster_builder.size.ring);  // worst case 1:1 (cmds:rasters)

  uint32_t const tc_ring_size =
    config->raster_builder.size.ring * 3;  // 1 transform + 1 clip = 3 quads

  spn_next_init(&impl->mapped.tc.next, tc_ring_size);

  size_t const cf_size = config->raster_builder.size.ring * sizeof(struct spn_cmd_fill);
  size_t const tc_size = tc_ring_size * sizeof(float[4]);
  size_t const rc_size = config->raster_builder.size.ring * sizeof(spn_raster_t);

  impl->vk.rings.cf.offset = 0;
  impl->vk.rings.cf.range  = cf_size;

  impl->vk.rings.tc.offset = cf_size;
  impl->vk.rings.tc.range  = tc_size;

  size_t const cf_tc_size = cf_size + tc_size;

  impl->vk.rings.rc.offset = cf_tc_size;
  impl->vk.rings.rc.range  = rc_size;

  size_t const vk_extent_size = cf_tc_size + rc_size;

  //
  // allocate and map rings
  //
  spn_allocator_device_perm_alloc(&device->allocator.device.perm.coherent,
                                  device->environment,
                                  vk_extent_size,
                                  NULL,
                                  &impl->vk.rings.h.dbi,
                                  &impl->vk.rings.h.dm);

  vk(MapMemory(device->environment->d,
               impl->vk.rings.h.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.cf.extent));

  impl->mapped.tc.extent = (void *)(impl->mapped.cf.extent + config->raster_builder.size.ring);
  impl->mapped.rc.extent = (void *)(impl->mapped.tc.extent + tc_ring_size);

  if (config->raster_builder.vk.rings.d != 0)  // FIXME -- this will be improved later
    {
      spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                      device->environment,
                                      vk_extent_size,
                                      NULL,
                                      &impl->vk.rings.d.dbi,
                                      &impl->vk.rings.d.dm);
    }
  else
    {
      impl->vk.rings.d.dbi = impl->vk.rings.h.dbi;
      impl->vk.rings.d.dm  = impl->vk.rings.h.dm;
    }

  VkBuffer const rings = impl->vk.rings.d.dbi.buffer;

  impl->vk.rings.cf.buffer = rings;
  impl->vk.rings.tc.buffer = rings;
  impl->vk.rings.rc.buffer = rings;

  //
  // allocate and map copyback
  //
  uint32_t const max_in_flight = config->fence_pool.size;
  size_t const   copyback_size = max_in_flight * sizeof(*impl->mapped.cb.extent);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.copyback,
                                  device->environment,
                                  copyback_size,
                                  NULL,
                                  &impl->vk.copyback.dbi,
                                  &impl->vk.copyback.dm);

  vk(MapMemory(device->environment->d,
               impl->vk.copyback.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.cb.extent));

  //
  // allocate release resources
  //
  size_t const dispatches_size = max_in_flight * sizeof(*impl->dispatches.extent);
  size_t const paths_size      = config->raster_builder.size.ring * sizeof(*impl->paths.extent);
  size_t const rasters_size    = config->raster_builder.size.ring * sizeof(*impl->rasters.extent);
  size_t const h_extent_size   = dispatches_size + paths_size + rasters_size;

  impl->dispatches.extent =
    spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, h_extent_size);

  impl->paths.extent   = (void *)(impl->dispatches.extent + max_in_flight);
  impl->rasters.extent = (void *)(impl->paths.extent + config->raster_builder.size.ring);

  spn_ring_init(&impl->dispatches.ring, max_in_flight);
  spn_next_init(&impl->paths.next, config->raster_builder.size.ring);
  spn_next_init(&impl->rasters.next, config->raster_builder.size.ring);

  spn_rbi_wip_init(impl);

  spn_rbi_dispatch_init(impl, impl->dispatches.extent);

  spn_weakref_epoch_init(&impl->epoch);

  return SPN_SUCCESS;
}

//
//
//
