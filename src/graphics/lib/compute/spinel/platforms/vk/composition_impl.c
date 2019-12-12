// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "composition_impl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block_pool.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "core_vk.h"
#include "device.h"
#include "dispatch.h"
#include "handle_pool.h"
#include "hotsort/platforms/vk/hotsort_vk.h"
#include "queue_pool.h"
#include "raster_builder_impl.h"
#include "ring.h"
#include "spinel_assert.h"
#include "state_assert.h"
#include "status.h"
#include "vk_target.h"

//
// composition states
//

typedef enum spn_ci_state_e
{
  SPN_CI_STATE_RESET,      // unsealed and was reset
  SPN_CI_STATE_RESETTING,  // unsealed and resetting
  SPN_CI_STATE_UNSEALED,   // ready to place rasters
  SPN_CI_STATE_SEALING,    // waiting for PLACE and TTCK_SORT
  SPN_CI_STATE_SEALED      // sort & segment complete

} spn_ci_state_e;

//
// The composition launches a number of dependent command buffers:
//
//   1. reset TTCK atomic count
//   2. PLACE shaders -- happens-after (1)
//   3. COPYBACK -- happens-after (2)
//   4. SORT -- happens-after (3)
//

//
// FIXME(allanmac): The scheduling logic has changed.
//
// There are always as many dispatch records as there are fences in
// the fence pool.  This simplifies reasoning about concurrency.
//
// The dispatch record in the composition tracks resources associated
// with wip and in-flight PLACE submissions.
//

typedef enum spn_ci_dispatch_state_e
{
  SPN_CI_DISPATCH_STATE_PLACING,
  SPN_CI_DISPATCH_STATE_PLACED

} spn_ci_dispatch_state_e;

//
//
//

struct spn_ci_dispatch
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

  spn_ci_dispatch_state_e state;

  bool unreleased;

  spn_dispatch_id_t id;
};

//
//
//

struct spn_ci_vk
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
  } rings;

  struct
  {
    VkDescriptorBufferInfo dbi;
    VkDeviceMemory         dm;
  } ttcks;

  struct
  {
    VkDescriptorBufferInfo dbi;
    VkDeviceMemory         dm;
  } copyback;
};

//
// This structure *partially* matches `struct spn_vk_buf_ttcks_ttcks`
//
// FIXME(allanmac): hoist this so that we always have a compatible C and
// GLSL structure instead of partially redefining it here.
//

struct spn_ci_copyback
{
  uint32_t ttcks_count[4];  // only first dword is used

#ifndef NDEBUG
  uint32_t offsets_count[4];  // first 3 dwords are used
#endif
};

//
//
//

struct spn_composition_impl
{
  struct spn_composition *            composition;
  struct spn_device *                 device;
  struct spn_vk_target_config const * config;  // FIXME(allanmac): we don't need to duplicate this
  struct spn_ci_vk                    vk;

  //
  // composition clip
  //
  struct spn_ivec4 clip;

  //
  // mapped command ring and copyback counts
  //
  struct
  {
    struct
    {
      struct spn_cmd_place * extent;
      struct spn_ring        ring;
    } cp;  // place commands

    struct
    {
      struct spn_ci_copyback * extent;
    } cb;
  } mapped;

  //
  // records of work-in-progress and work-in-flight
  //
  struct
  {
    struct spn_ci_dispatch * extent;
    struct spn_ring          ring;
  } dispatches;

  //
  // all rasters are retained until reset or release
  //
  struct
  {
    spn_handle_t * extent;
    uint32_t       size;
    uint32_t       count;
  } rasters;

  uint32_t lock_count;  // # of wip renders

  SPN_ASSERT_STATE_DECLARE(spn_ci_state_e);

  //
  // dispatch ids
  //
  spn_dispatch_id_t id_sealing;
  spn_dispatch_id_t id_resetting;
};

//
// A dispatch captures how many paths and blocks are in a dispatched or
// the work-in-progress compute grid.
//

static struct spn_ci_dispatch *
spn_ci_dispatch_idx(struct spn_composition_impl * const impl, uint32_t const idx)
{
  return impl->dispatches.extent + idx;
}

static struct spn_ci_dispatch *
spn_ci_dispatch_head(struct spn_composition_impl * const impl)
{
  return spn_ci_dispatch_idx(impl, impl->dispatches.ring.head);
}

static struct spn_ci_dispatch *
spn_ci_dispatch_tail(struct spn_composition_impl * const impl)
{
  return spn_ci_dispatch_idx(impl, impl->dispatches.ring.tail);
}

static bool
spn_ci_dispatch_is_empty(struct spn_ci_dispatch const * const dispatch)
{
  return dispatch->cp.span == 0;
}

static void
spn_ci_dispatch_init(struct spn_composition_impl * const impl,
                     struct spn_ci_dispatch * const      dispatch)
{
  dispatch->cp.head    = impl->mapped.cp.ring.head;
  dispatch->cp.span    = 0;
  dispatch->rd.head    = impl->rasters.count;
  dispatch->state      = SPN_CI_DISPATCH_STATE_PLACING;
  dispatch->unreleased = false;

  spn(device_dispatch_acquire(impl->device, SPN_DISPATCH_STAGE_COMPOSITION_PLACE, &dispatch->id));
}

static void
spn_ci_dispatch_drop(struct spn_composition_impl * const impl)
{
  struct spn_ring * const ring = &impl->dispatches.ring;

  spn_ring_drop_1(ring);

  while (spn_ring_is_empty(ring))
    {
      SPN_DEVICE_WAIT(impl->device);
    }

  struct spn_ci_dispatch * const dispatch = spn_ci_dispatch_idx(impl, ring->head);

  spn_ci_dispatch_init(impl, dispatch);
}

//
// Wait on all in-flight PLACE before SEALING_1
//

static void
spn_ci_dispatch_wait(struct spn_composition_impl * const impl, spn_dispatch_id_t const id)
{
  struct spn_ring * const ring      = &impl->dispatches.ring;
  uint32_t                in_flight = spn_ring_dropped(ring);

  // anything to do?
  if (in_flight == 0)
    return;

  uint32_t                             tail       = ring->tail;
  uint32_t const                       size       = ring->size;
  struct spn_ci_dispatch const * const dispatches = impl->dispatches.extent;
  struct spn_device * const            device     = impl->device;

  for (uint32_t ii = 0; ii < in_flight; ii++)
    {
      struct spn_ci_dispatch const * const dispatch = dispatches + tail++;

      if (dispatch->state == SPN_CI_DISPATCH_STATE_PLACING)
        {
          spn_device_dispatch_happens_after(device, id, dispatch->id);
        }

      if (tail == size)
        {
          tail = 0;
        }
    }
}

//
// COMPLETION: PLACE
//

struct spn_ci_complete_payload_place
{
  struct spn_composition_impl * impl;

  struct
  {
    struct spn_vk_ds_ttcks_t ttcks;
    struct spn_vk_ds_place_t place;
  } ds;

  uint32_t dispatch_idx;  // dispatch idx
};

//
//
//

static void
spn_ci_complete_place(void * pfn_payload)
{
  struct spn_ci_complete_payload_place const * const payload  = pfn_payload;
  struct spn_composition_impl * const                impl     = payload->impl;
  struct spn_device * const                          device   = impl->device;
  struct spn_vk * const                              instance = device->instance;

  // release descriptor sets
  spn_vk_ds_release_ttcks(instance, payload->ds.ttcks);
  spn_vk_ds_release_place(instance, payload->ds.place);

  //
  // If the dispatch is the tail of the ring then try to release as
  // many dispatch records as possible...
  //
  // Note that kernels can complete in any order so the release
  // records need to add to the mapped.ring.tail in order.
  //
  uint32_t const           dispatch_idx = payload->dispatch_idx;
  struct spn_ci_dispatch * dispatch     = spn_ci_dispatch_idx(impl, dispatch_idx);

  dispatch->state = SPN_CI_DISPATCH_STATE_PLACED;

  if (spn_ring_is_tail(&impl->dispatches.ring, dispatch_idx))
    {
      do
        {
          spn_ring_release_n(&impl->mapped.cp.ring, dispatch->cp.span);
          spn_ring_release_n(&impl->dispatches.ring, 1);

          dispatch = spn_ci_dispatch_tail(impl);
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
spn_ci_flush(struct spn_composition_impl * const impl)
{
  struct spn_ci_dispatch * const dispatch = spn_ci_dispatch_head(impl);

  // is this a dispatch with no commands?
  if (spn_ci_dispatch_is_empty(dispatch))
    return;

  //
  // We're go for launch...
  //
  struct spn_device * const device = impl->device;

  //
  // get the cb associated with the wip dispatch
  //
  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, dispatch->id);

  //
  // COPY COMMANDS
  //
  // If this is a discrete GPU, copy the place command ring.
  //
  if (impl->config->composition.vk.rings.d != 0)
    {
      VkDeviceSize const head_offset = dispatch->cp.head * sizeof(struct spn_cmd_place);

      if (dispatch->cp.head + dispatch->cp.span <= impl->mapped.cp.ring.size)
        {
          VkBufferCopy bcs[1];

          bcs[0].srcOffset = impl->vk.rings.h.dbi.offset + head_offset;
          bcs[0].dstOffset = impl->vk.rings.d.dbi.offset + head_offset;
          bcs[0].size      = dispatch->cp.span * sizeof(struct spn_cmd_place);

          vkCmdCopyBuffer(cb, impl->vk.rings.h.dbi.buffer, impl->vk.rings.d.dbi.buffer, 1, bcs);
        }
      else  // wraps around ring
        {
          VkBufferCopy bcs[2];

          uint32_t const hi = impl->mapped.cp.ring.size - dispatch->cp.head;
          bcs[0].srcOffset  = impl->vk.rings.h.dbi.offset + head_offset;
          bcs[0].dstOffset  = impl->vk.rings.d.dbi.offset + head_offset;
          bcs[0].size       = hi * sizeof(struct spn_cmd_place);

          uint32_t const lo = dispatch->cp.head + dispatch->cp.span - impl->mapped.cp.ring.size;
          bcs[1].srcOffset  = impl->vk.rings.h.dbi.offset;
          bcs[1].dstOffset  = impl->vk.rings.d.dbi.offset;
          bcs[1].size       = lo * sizeof(struct spn_cmd_place);

          vkCmdCopyBuffer(cb, impl->vk.rings.h.dbi.buffer, impl->vk.rings.d.dbi.buffer, 2, bcs);
        }

      vk_barrier_transfer_w_to_compute_r(cb);
    }

  //
  // DS: BLOCK POOL
  //
  struct spn_vk * const instance = device->instance;

  spn_vk_ds_bind_place_ttpk_block_pool(instance, cb, spn_device_block_pool_get_ds(device));

  //
  // DS: TTCKS
  //
  // acquire TTCKS descriptor set
  struct spn_vk_ds_ttcks_t ds_ttcks;

  spn_vk_ds_acquire_ttcks(instance, device, &ds_ttcks);

  // copy the dbi structs
  *spn_vk_ds_get_ttcks_ttcks(instance, ds_ttcks) = impl->vk.ttcks.dbi;

  // update TTCKS descriptor set
  spn_vk_ds_update_ttcks(instance, &device->environment, ds_ttcks);

  // bind the TTCKS descriptor set
  spn_vk_ds_bind_place_ttpk_ttcks(instance, cb, ds_ttcks);

  //
  // DS: PLACE
  //
  // acquire PLACE descriptor set
  struct spn_vk_ds_place_t ds_place;

  spn_vk_ds_acquire_place(instance, device, &ds_place);

  // copy the dbi struct
  *spn_vk_ds_get_place_place(instance, ds_place) = impl->vk.rings.d.dbi;

  // update PLACE descriptor set
  spn_vk_ds_update_place(instance, &device->environment, ds_place);

  // bind PLACE descriptor set
  spn_vk_ds_bind_place_ttpk_place(instance, cb, ds_place);

  //
  // set a completion payload
  //
  struct spn_ci_complete_payload_place * const payload =
    spn_device_dispatch_set_completion(device,
                                       dispatch->id,
                                       spn_ci_complete_place,
                                       sizeof(*payload));

  payload->impl         = impl;
  payload->ds.ttcks     = ds_ttcks;
  payload->ds.place     = ds_place;
  payload->dispatch_idx = impl->dispatches.ring.head;

  //
  // PIPELINE: PLACE
  //
  // Set up push constants -- note that for now the paths_copy push
  // constants are an extension of the paths_alloc constants.
  //
  // This means we can push the constants once.
  //
  struct spn_vk_push_place_ttpk const push = {

    .place_clip = impl->clip,
    .place_head = dispatch->cp.head,
    .place_span = dispatch->cp.span,
    .place_size = impl->mapped.cp.ring.size
  };

  spn_vk_p_push_place_ttpk(instance, cb, &push);

  // dispatch one subgroup per command -- place_ttpk and place_ttsk are same
  uint32_t const place_wg_size      = impl->config->p.group_sizes.named.place_ttpk.workgroup;
  uint32_t const place_sg_size_log2 = impl->config->p.group_sizes.named.place_ttpk.subgroup_log2;
  uint32_t const place_cmds_per_wg  = place_wg_size >> place_sg_size_log2;

  uint32_t const place_wgs = (dispatch->cp.span + place_cmds_per_wg - 1) / place_cmds_per_wg;

  // bind PLACE_TTPK
  spn_vk_p_bind_place_ttpk(instance, cb);

  // dispatch PLACE_TTPK
  vkCmdDispatch(cb, place_wgs, 1, 1);

  // bind PLACE_TTSK
  spn_vk_p_bind_place_ttsk(instance, cb);

  // dispatch PLACE_TTSK
  vkCmdDispatch(cb, place_wgs, 1, 1);

  //
  // Wait for reset
  //
  if (impl->state == SPN_CI_STATE_RESETTING)
    {
      spn_device_dispatch_happens_after(device, dispatch->id, impl->id_resetting);
    }

  //
  // Wait for rasters associated with this dispatch to materialize
  //
  spn_device_dispatch_happens_after_handles_and_submit(device,
                                                       (spn_dispatch_flush_pfn_t)spn_rbi_flush,
                                                       dispatch->id,
                                                       impl->rasters.extent + dispatch->rd.head,
                                                       UINT32_MAX,
                                                       dispatch->cp.span,
                                                       0);
  //
  // The current dispatch is now "in flight" so drop it and try to
  // acquire and initialize the next.
  //
  spn_ci_dispatch_drop(impl);
}

//
// COMPLETION: SEALING
//
//   PHASE 1: COPYBACK
//   PHASE 2: SORT & SEGMENT
//
// The same payload is used for both phases
//

struct spn_ci_complete_payload_sealing
{
  struct spn_composition_impl * impl;

  struct
  {
    struct spn_vk_ds_ttcks_t ttcks;
  } ds;
};

//
//
//

static void
spn_ci_complete_sealing_2(void * pfn_payload)
{
  struct spn_ci_complete_payload_sealing const * const payload  = pfn_payload;
  struct spn_composition_impl * const                  impl     = payload->impl;
  struct spn_device * const                            device   = impl->device;
  struct spn_vk * const                                instance = device->instance;

  // release the ttcks ds -- will never wait()
  spn_vk_ds_release_ttcks(instance, payload->ds.ttcks);

  // move to sealed state
  impl->state = SPN_CI_STATE_SEALED;

  //
  // DEBUG
  //
#if !defined(NDEBUG) && 0
  fprintf(stderr,
          "offsets_count = { %u, %u, %u }\n",
          impl->mapped.cb.extent->offsets_count[0],
          impl->mapped.cb.extent->offsets_count[1],
          impl->mapped.cb.extent->offsets_count[2]);
#endif
}

//
//
//

static void
spn_ci_complete_sealing_1(void * pfn_payload)
{
  struct spn_ci_complete_payload_sealing const * const payload = pfn_payload;

  struct spn_composition_impl * const impl     = payload->impl;
  struct spn_device * const           device   = impl->device;
  struct spn_vk * const               instance = device->instance;

  //
  // duplicate the completion payload
  //
  struct spn_ci_complete_payload_sealing * const payload_copy =
    spn_device_dispatch_set_completion(device,
                                       impl->id_sealing,
                                       spn_ci_complete_sealing_2,
                                       sizeof(*payload_copy));
  *payload_copy = *payload;

  //
  // get a cb
  //
  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, impl->id_sealing);

  //
  // DEBUG ONLY
  //
  // This DS only needs to be bound if we're debugging
  //
#ifndef NDEBUG
  //
  // BLOCK POOL
  //
  // bind global BLOCK_POOL descriptor set
  spn_vk_ds_bind_segment_ttck_block_pool(instance, cb, spn_device_block_pool_get_ds(device));
#endif

  //
  // DS: TTCKS
  //
  spn_vk_ds_bind_segment_ttck_ttcks(instance, cb, payload->ds.ttcks);

  ////////////////////////////////////////////////////////////////
  //
  // HOTSORT
  //
  ////////////////////////////////////////////////////////////////

#if 0
  //
  // FIXME(allanmac): evaluate cached coherent vs. invalidated
  //
  VkMappedMemoryRange const mmrs[] = {

    { .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .pNext  = NULL,
      .memory = impl->vk.copyback.dm,
      .offset = 0,
      .size   = VK_WHOLE_SIZE }
  };

  vk(InvalidateMappedMemoryRanges(device->environment->d, 1, mmrs));
#endif

  uint32_t const keys_count = impl->mapped.cb.extent->ttcks_count[0];
  uint32_t       slabs_in;
  uint32_t       padded_in;
  uint32_t       padded_out;

  hotsort_vk_pad(device->hs, keys_count, &slabs_in, &padded_in, &padded_out);

#if !defined(NDEBUG) && 0
  fprintf(stderr,
          "keys_count = %u\n"
          "slabs_in   = %u\n"
          "padded_in  = %u\n"
          "padded_out = %u\n",
          keys_count,
          slabs_in,
          padded_in,
          padded_out);
#endif

  struct hotsort_vk_ds_offsets const keys_offsets = {
    .in  = SPN_VK_BUFFER_OFFSETOF(ttcks, ttcks, ttcks_keys),
    .out = SPN_VK_BUFFER_OFFSETOF(ttcks, ttcks, ttcks_keys)
  };

  hotsort_vk_sort(cb, device->hs, &keys_offsets, keys_count, padded_in, padded_out, false);

  vk_barrier_compute_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // PIPELINE: SEGMENT_TTCK
  //
  ////////////////////////////////////////////////////////////////

  // bind the pipeline
  spn_vk_p_bind_segment_ttck(instance, cb);

  // dispatch one workgroup per fill command
  vkCmdDispatch(cb, slabs_in, 1, 1);

//
// DEBUG -- COPYBACK TO INSPECT OFFSETS COUNT
//
#ifndef NDEBUG
  vk_barrier_compute_w_to_transfer_r(cb);

  VkDeviceSize const dbi_src_offset = impl->vk.ttcks.dbi.offset;
  VkDeviceSize const dbi_dst_offset = impl->vk.copyback.dbi.offset;

  VkBufferCopy const bc = {

    .srcOffset = dbi_src_offset + SPN_VK_BUFFER_OFFSETOF(ttcks, ttcks, offsets_count),
    .dstOffset = dbi_dst_offset + OFFSETOF_MACRO(struct spn_ci_copyback, offsets_count),
    .size      = sizeof(impl->mapped.cb.extent->offsets_count)
  };

  // copyback the key count
  vkCmdCopyBuffer(cb, impl->vk.ttcks.dbi.buffer, impl->vk.copyback.dbi.buffer, 1, &bc);
#endif

  //
  // submit the dispatch
  //
  spn_device_dispatch_submit(device, impl->id_sealing);
}

//
//
//

static void
spn_ci_unsealed_to_sealing(struct spn_composition_impl * const impl)
{
  //
  // update the state
  //
  impl->state = SPN_CI_STATE_SEALING;

  //
  // acquire the sealing dispatch id ahead of time
  //
  struct spn_device * const device = impl->device;

  spn(device_dispatch_acquire(device, SPN_DISPATCH_STAGE_COMPOSITION_SEAL_2, &impl->id_sealing));

  //
  // flush the current dispatch
  //
  spn_ci_flush(impl);

  struct spn_vk * const instance = device->instance;

  //
  // acquire a dispatch to kick off phase 1 of sealing
  //
  spn_dispatch_id_t id;

  spn(device_dispatch_acquire(device, SPN_DISPATCH_STAGE_COMPOSITION_SEAL_1, &id));

  //
  // wait on any in-flight PLACE dispatches
  //
  spn_ci_dispatch_wait(impl, id);

  // get a cb
  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, id);

  //
  // set a completion payload
  //
  struct spn_ci_complete_payload_sealing * const payload_sealing =
    spn_device_dispatch_set_completion(device,
                                       id,
                                       spn_ci_complete_sealing_1,
                                       sizeof(*payload_sealing));

  payload_sealing->impl = impl;

  //
  // DS: TTCKS
  //
  // FIXME(allanmac): do we need to acquire this DS here and so early?
  //

  // acquire TTCKS descriptor set
  spn_vk_ds_acquire_ttcks(instance, device, &payload_sealing->ds.ttcks);

  // copy the dbi structs
  *spn_vk_ds_get_ttcks_ttcks(instance, payload_sealing->ds.ttcks) = impl->vk.ttcks.dbi;

  // update TTCKS descriptor set
  spn_vk_ds_update_ttcks(instance, &device->environment, payload_sealing->ds.ttcks);

  //
  // INITIALIZE DISPATCH INDIRECT BUFFER
  //
  // FIXME(allanmac): This could be done much earlier but it probably doesn't
  // matter.  Evaluate once we can measure and visualize queue submissions.
  //
  VkDeviceSize const dbi_ttcks_offset = impl->vk.ttcks.dbi.offset;

  uint32_t const dispatch_indirect[] = { 0, 1, 1, 0 };

  vkCmdUpdateBuffer(cb,
                    impl->vk.ttcks.dbi.buffer,
                    dbi_ttcks_offset + SPN_VK_BUFFER_OFFSETOF(ttcks, ttcks, offsets_count),
                    sizeof(dispatch_indirect),
                    dispatch_indirect);

  //
  // COPYBACK TTCKS_COUNT
  //
  VkDeviceSize const dbi_copyback_offset = impl->vk.copyback.dbi.offset;

  VkBufferCopy const bc = {

    .srcOffset = dbi_ttcks_offset + SPN_VK_BUFFER_OFFSETOF(ttcks, ttcks, ttcks_count),
    .dstOffset = dbi_copyback_offset + OFFSETOF_MACRO(struct spn_ci_copyback, ttcks_count),
    .size      = sizeof(impl->mapped.cb.extent->ttcks_count)
  };

  // copyback the key count
  vkCmdCopyBuffer(cb, impl->vk.ttcks.dbi.buffer, impl->vk.copyback.dbi.buffer, 1, &bc);

  //
  // FIXME(allanmac): verify whether this is necessary with host
  // coherent memory.
  //
  // make the copyback visible to the host
  vk_barrier_transfer_w_to_host_r(cb);

  //
  // submit the dispatch
  //
  spn_device_dispatch_submit(device, id);
}

//
//
//

struct spn_ci_complete_reset_payload
{
  struct spn_composition_impl * impl;
};

static void
spn_ci_complete_reset(void * pfn_payload)
{
  struct spn_ci_complete_reset_payload const * const payload = pfn_payload;
  struct spn_composition_impl * const                impl    = payload->impl;

  if (impl->rasters.count > 0)
    {
      //
      // release any retained rasters
      //
      spn_device_handle_pool_release_d_rasters(impl->device,
                                               impl->rasters.extent,
                                               impl->rasters.count);

      //
      // zero the count
      //
      impl->rasters.count = 0;

      //
      // reset the WIP dispatch
      //
      struct spn_ci_dispatch * const dispatch = spn_ci_dispatch_head(impl);

      dispatch->cp.span = 0;
      dispatch->rd.head = 0;
    }

  //
  // move to RESET state
  //
  payload->impl->state = SPN_CI_STATE_RESET;
}

//
//
//

static void
spn_ci_unsealed_reset(struct spn_composition_impl * const impl)
{
  //
  // otherwise... kick off a zeroing fill
  //
  impl->state = SPN_CI_STATE_RESETTING;

  //
  // acquire a dispatch
  //
  struct spn_device * const device = impl->device;

  spn(device_dispatch_acquire(device, SPN_DISPATCH_STAGE_COMPOSITION_RESET, &impl->id_resetting));

  // get a cb
  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, impl->id_resetting);

  //
  // zero ttcks_count
  //
  VkDeviceSize const dbi_src_offset = impl->vk.ttcks.dbi.offset;

  vkCmdFillBuffer(cb,
                  impl->vk.ttcks.dbi.buffer,
                  dbi_src_offset + SPN_VK_BUFFER_OFFSETOF(ttcks, ttcks, ttcks_count),
                  SPN_VK_BUFFER_MEMBER_SIZE(ttcks, ttcks, ttcks_count),
                  0);
  //
  // set a completion payload
  //
  struct spn_ci_complete_reset_payload * const payload =
    spn_device_dispatch_set_completion(device,
                                       impl->id_resetting,
                                       spn_ci_complete_reset,
                                       sizeof(*payload));
  payload->impl = impl;

  //
  // submit the dispatch
  //
  spn_device_dispatch_submit(device, impl->id_resetting);
}

//
//
//

static void
spn_ci_block_until_sealed(struct spn_composition_impl * const impl)
{
  struct spn_device * const device = impl->device;

  while (impl->state != SPN_CI_STATE_SEALED)
    {
      SPN_DEVICE_WAIT(device);
    }
}

static void
spn_ci_sealed_unseal(struct spn_composition_impl * const impl)
{
  //
  // wait for any in-flight renders to complete
  //
  struct spn_device * const device = impl->device;

  while (impl->lock_count > 0)
    {
      SPN_DEVICE_WAIT(device);
    }

  impl->state = SPN_CI_STATE_UNSEALED;
}

//
// FIXME(allanmac): add UNSEALING state
//

static spn_result_t
spn_ci_seal(struct spn_composition_impl * const impl)
{
  switch (impl->state)
    {
      case SPN_CI_STATE_RESET:
      case SPN_CI_STATE_RESETTING:
      case SPN_CI_STATE_UNSEALED:
        spn_ci_unsealed_to_sealing(impl);
        return SPN_SUCCESS;

      case SPN_CI_STATE_SEALING:
        return SPN_SUCCESS;

      case SPN_CI_STATE_SEALED:
        // default:
        return SPN_SUCCESS;
    }
}

static spn_result_t
spn_ci_unseal(struct spn_composition_impl * const impl)
{
  switch (impl->state)
    {
      case SPN_CI_STATE_RESET:
      case SPN_CI_STATE_RESETTING:
      case SPN_CI_STATE_UNSEALED:
        return SPN_SUCCESS;

      case SPN_CI_STATE_SEALING:
        spn_ci_block_until_sealed(impl);
        // [[fallthrough]];

      case SPN_CI_STATE_SEALED:
        // default:
        spn_ci_sealed_unseal(impl);
        return SPN_SUCCESS;
    }
}

static spn_result_t
spn_ci_reset(struct spn_composition_impl * const impl)
{
  switch (impl->state)
    {
      case SPN_CI_STATE_RESET:
      case SPN_CI_STATE_RESETTING:
        return SPN_SUCCESS;

      case SPN_CI_STATE_UNSEALED:
        spn_ci_unsealed_reset(impl);
        return SPN_SUCCESS;

      case SPN_CI_STATE_SEALING:
        return SPN_ERROR_RASTER_BUILDER_SEALED;

      case SPN_CI_STATE_SEALED:
        // default:
        return SPN_ERROR_RASTER_BUILDER_SEALED;
    }
}

//
//
//

static spn_result_t
spn_ci_clone(struct spn_composition_impl * const impl, struct spn_composition ** const clone)
{
  return SPN_ERROR_NOT_IMPLEMENTED;
}

//
//
//

static spn_result_t
spn_ci_get_bounds(struct spn_composition_impl * const impl, uint32_t bounds[4])
{
  return SPN_ERROR_NOT_IMPLEMENTED;
}

//
// Initialize clip to max tile clip for the target
//

static void
spn_ci_get_max_clip(struct spn_composition_impl * const impl, struct spn_ivec4 * const clip)
{
  *clip = (struct spn_ivec4){ .x = 0,
                              .y = 0,
                              .z = 1 << SPN_TTCK_HI_BITS_X,
                              .w = 1 << SPN_TTCK_HI_BITS_Y };
}

//
//
//

static spn_result_t
spn_ci_set_clip(struct spn_composition_impl * const impl, uint32_t const clip[4])
{
  switch (impl->state)
    {
      case SPN_CI_STATE_RESET:
        break;

      case SPN_CI_STATE_RESETTING:
        do
          {
            SPN_DEVICE_WAIT(impl->device);
          }
        while (impl->state == SPN_CI_STATE_RESETTING);
        break;

      case SPN_CI_STATE_UNSEALED:
        break;

      case SPN_CI_STATE_SEALING:
      case SPN_CI_STATE_SEALED:
      default:
        return SPN_ERROR_RASTER_BUILDER_SEALED;
    }

  //
  // convert pixel clip coords to tile coords
  //
  // FIXME(allanmac): use the signed SIMD4 trick
  //
  uint32_t const tile_w = 1 << impl->config->tile.width_log2;
  uint32_t const tile_h = 1 << impl->config->tile.height_log2;

  uint32_t const surf_w_max = tile_w << SPN_TTCK_HI_BITS_X;
  uint32_t const surf_h_max = tile_h << SPN_TTCK_HI_BITS_Y;

  struct spn_uvec4 const tile_clip = {

    clip[0] >> impl->config->tile.width_log2,
    clip[1] >> impl->config->tile.height_log2,

    (MIN_MACRO(uint32_t, clip[2], surf_w_max) + tile_w - 1) >> impl->config->tile.width_log2,
    (MIN_MACRO(uint32_t, clip[3], surf_h_max) + tile_h - 1) >> impl->config->tile.height_log2
  };

  // clip to max
  impl->clip.x = MIN_MACRO(uint32_t, tile_clip.x, 1 << SPN_TTCK_HI_BITS_X);
  impl->clip.y = MIN_MACRO(uint32_t, tile_clip.y, 1 << SPN_TTCK_HI_BITS_Y);
  impl->clip.z = MIN_MACRO(uint32_t, tile_clip.z, 1 << SPN_TTCK_HI_BITS_X);
  impl->clip.w = MIN_MACRO(uint32_t, tile_clip.w, 1 << SPN_TTCK_HI_BITS_Y);

  return SPN_SUCCESS;
}

//
//
//

static spn_result_t
spn_ci_place(struct spn_composition_impl * const impl,
             spn_raster_t const *                rasters,
             spn_layer_id const *                layer_ids,
             spn_txty_t const *                  txtys,
             uint32_t                            count)
{
  switch (impl->state)
    {
      case SPN_CI_STATE_RESET:
        break;

      case SPN_CI_STATE_RESETTING:
        do
          {
            SPN_DEVICE_WAIT(impl->device);
          }
        while (impl->state == SPN_CI_STATE_RESETTING);
        break;

      case SPN_CI_STATE_UNSEALED:
        break;

      case SPN_CI_STATE_SEALING:
      case SPN_CI_STATE_SEALED:
      default:
        return SPN_ERROR_RASTER_BUILDER_SEALED;
    }

  // nothing to do?
  if (count == 0)
    {
      return SPN_SUCCESS;
    }

  // validate there is enough room for rasters
  if (impl->rasters.count + count > impl->rasters.size)
    {
      return SPN_ERROR_COMPOSITION_TOO_MANY_RASTERS;
    }

#if 0
  //
  // FIXME -- No, we should NEVER need to do this.  The layer invoking
  // Spinel should ensure that layer ids remain in range.  Do not
  // enable this.
  //
  // validate layer ids
  //
  for (uint32_t ii=0; ii<count; ii++) {
    if (layer_ids[ii] > SPN_TTCK_LAYER_MAX) {
      return SPN_ERROR_LAYER_ID_INVALID;
    }
  }
#endif

  //
  // validate first and then retain the rasters before we proceed
  //
  struct spn_device * const device = impl->device;

  spn_result_t result;

  result = spn_device_handle_pool_validate_d_rasters(device, rasters, count);

  if (result != SPN_SUCCESS)
    return result;

  spn_device_handle_pool_retain_d_rasters(device, rasters, count);

  //
  // No survivable errors from here onward... any failure beyond here
  // will be fatal to the context -- most likely too many ttcks.
  //

  //
  // block if resetting...
  //
  while (impl->state == SPN_CI_STATE_RESETTING)
    {
      SPN_DEVICE_WAIT(device);  // FIXME(allanmac): wait on resetting id
    }

  //
  // save the untyped raster handles
  //
  spn_handle_t * const saved = impl->rasters.extent + impl->rasters.count;

  impl->rasters.count += count;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      saved[ii] = rasters[ii].handle;
    }

  //
  // copy place commands into the ring
  //
  struct spn_ring * const ring = &impl->mapped.cp.ring;

  while (true)
    {
      //
      // how many slots left in ring?
      //
      uint32_t avail = MIN_MACRO(uint32_t, count, spn_ring_rem_nowrap(ring));

      //
      // if ring is full then this implies we're already waiting on
      // dispatches because an eager launch would've occurred
      //
      if (avail == 0)
        {
          SPN_DEVICE_WAIT(device);
          continue;
        }

      //
      // increment dispatch span
      //
      struct spn_ci_dispatch * const dispatch = spn_ci_dispatch_head(impl);

      dispatch->cp.span += avail;

      //
      // append commands
      //
      struct spn_cmd_place * cmds = impl->mapped.cp.extent + ring->head;

      spn_ring_drop_n(ring, avail);

      count -= avail;

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
      if (dispatch->cp.span >= impl->config->composition.size.eager)
        {
          spn_ci_flush(impl);
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

static spn_result_t
spn_ci_release(struct spn_composition_impl * const impl)
{
  //
  // was this the last reference?
  //
  if (--impl->composition->ref_count != 0)
    {
      return SPN_SUCCESS;
    }

  struct spn_device * const device = impl->device;

  //
  // wait for any in-flight PLACE dispatches to complete
  //
  while (!spn_ring_is_full(&impl->dispatches.ring))
    {
      SPN_DEVICE_WAIT(device);
    }

  //
  // wait for any in-flight renders to complete
  //
  while (impl->lock_count > 0)
    {
      SPN_DEVICE_WAIT(device);
    }

  //
  // release any retained rasters
  //
  if (impl->rasters.count > 0)
    {
      spn_device_handle_pool_release_d_rasters(impl->device,
                                               impl->rasters.extent,
                                               impl->rasters.count);
    }

  //
  // note that we don't have to unmap before freeing
  //

  //
  // free copyback
  //
  spn_allocator_device_perm_free(&device->allocator.device.perm.copyback,
                                 &device->environment,
                                 &impl->vk.copyback.dbi,
                                 impl->vk.copyback.dm);
  //
  // free ttcks
  //
  spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                 &device->environment,
                                 &impl->vk.ttcks.dbi,
                                 impl->vk.ttcks.dm);
  //
  // free ring
  //
  if (impl->config->composition.vk.rings.d != 0)
    {
      spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                     &device->environment,
                                     &impl->vk.rings.d.dbi,
                                     impl->vk.rings.d.dm);
    }

  spn_allocator_device_perm_free(&device->allocator.device.perm.coherent,
                                 &device->environment,
                                 &impl->vk.rings.h.dbi,
                                 impl->vk.rings.h.dm);
  //
  // free host allocations
  //
  struct spn_allocator_host_perm * const perm = &impl->device->allocator.host.perm;

  spn_allocator_host_perm_free(perm, impl->rasters.extent);
  spn_allocator_host_perm_free(perm, impl->dispatches.extent);
  spn_allocator_host_perm_free(perm, impl->composition);
  spn_allocator_host_perm_free(perm, impl);

  return SPN_SUCCESS;
}

//
//
//

spn_result_t
spn_composition_impl_create(struct spn_device * const       device,
                            struct spn_composition ** const composition)
{
  //
  // FIXME(allanmac): retain the context
  //
  // spn_context_retain(context);
  //
  struct spn_allocator_host_perm * const perm = &device->allocator.host.perm;

  //
  // allocate impl
  //
  struct spn_composition_impl * const impl =
    spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, sizeof(*impl));
  //
  // allocate composition
  //
  struct spn_composition * const c =
    spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, sizeof(*c));

  // init impl and pb back-pointers
  *composition      = c;
  impl->composition = c;
  c->impl           = impl;

  // save device
  impl->device = device;

  struct spn_vk_target_config const * const config = spn_vk_get_config(device->instance);

  impl->config = config;

  impl->lock_count = 0;

  // the composition impl starts out unsealed
  SPN_ASSERT_STATE_INIT(SPN_CI_STATE_UNSEALED, impl);

  //
  // initialize composition
  //
  c->release    = spn_ci_release;
  c->place      = spn_ci_place;
  c->seal       = spn_ci_seal;
  c->unseal     = spn_ci_unseal;
  c->reset      = spn_ci_reset;
  c->clone      = spn_ci_clone;
  c->get_bounds = spn_ci_get_bounds;
  c->set_clip   = spn_ci_set_clip;
  c->ref_count  = 1;

  // set max clip
  spn_ci_get_max_clip(impl, &impl->clip);

  //
  // allocate and map ring
  //
  size_t const ring_size = config->composition.size.ring * sizeof(*impl->mapped.cp.extent);

  spn_ring_init(&impl->mapped.cp.ring, config->composition.size.ring);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.coherent,
                                  &device->environment,
                                  ring_size,
                                  NULL,
                                  &impl->vk.rings.h.dbi,
                                  &impl->vk.rings.h.dm);

  vk(MapMemory(device->environment.d,
               impl->vk.rings.h.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&impl->mapped.cp.extent));

  if (config->composition.vk.rings.d != 0)
    {
      spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                      &device->environment,
                                      ring_size,
                                      NULL,
                                      &impl->vk.rings.d.dbi,
                                      &impl->vk.rings.d.dm);
    }
  else
    {
      impl->vk.rings.d.dbi = impl->vk.rings.h.dbi;
      impl->vk.rings.d.dm  = impl->vk.rings.h.dm;
    }

  //
  // allocate ttck descriptor
  //
  size_t const ttcks_size = SPN_VK_BUFFER_OFFSETOF(ttcks, ttcks, ttcks_keys) +
                            config->composition.size.ttcks * sizeof(SPN_TYPE_UVEC2);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                  &device->environment,
                                  ttcks_size,
                                  NULL,
                                  &impl->vk.ttcks.dbi,
                                  &impl->vk.ttcks.dm);

  //
  // allocate and map tiny copyback buffer
  //
  size_t const copyback_size = sizeof(*impl->mapped.cb.extent);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.copyback,
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
  uint32_t const max_in_flight = config->composition.size.dispatches;
  size_t const   d_size        = sizeof(*impl->dispatches.extent) * max_in_flight;
  size_t const   r_size        = sizeof(*impl->rasters.extent) * config->composition.size.rasters;

  impl->dispatches.extent = spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, d_size);

  impl->rasters.extent = spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, r_size);
  impl->rasters.size   = config->composition.size.rasters;
  impl->rasters.count  = 0;

  spn_ring_init(&impl->dispatches.ring, max_in_flight);

  // initialize the first dispatch
  spn_ci_dispatch_init(impl, impl->dispatches.extent);

  // start in the resetting state
  spn_ci_unsealed_reset(impl);

  return SPN_SUCCESS;
}

//
//
//

static void
spn_ci_retain_and_lock(struct spn_composition_impl * const impl)
{
  impl->composition->ref_count += 1;

  impl->lock_count += 1;
}

static void
spn_composition_unlock_and_release(struct spn_composition_impl * const impl)
{
  impl->lock_count -= 1;

  spn_ci_release(impl);
}

//
//
//

void
spn_composition_happens_before(struct spn_composition * const composition,
                               spn_dispatch_id_t const        id)
{
  struct spn_composition_impl * const impl = composition->impl;

  assert(impl->state >= SPN_CI_STATE_SEALING);

  //
  // retain the composition
  //
  spn_ci_retain_and_lock(impl);

  //
  // already sealed?
  //
  if (impl->state == SPN_CI_STATE_SEALED)
    return;

  //
  // otherwise... composition happens before render
  //
  spn_device_dispatch_happens_after(impl->device,
                                    id,                 // after
                                    impl->id_sealing);  // before
}

//
//
//

void
spn_composition_pre_render_bind_ds(struct spn_composition * const   composition,
                                   struct spn_vk_ds_ttcks_t * const ds,
                                   VkCommandBuffer                  cb)
{
  struct spn_composition_impl * const impl     = composition->impl;
  struct spn_device * const           device   = impl->device;
  struct spn_vk * const               instance = device->instance;

  assert(impl->state >= SPN_CI_STATE_SEALING);

  //
  // acquire TTCKS descriptor set
  //
  spn_vk_ds_acquire_ttcks(instance, device, ds);

  // copy the dbi structs
  *spn_vk_ds_get_ttcks_ttcks(instance, *ds) = impl->vk.ttcks.dbi;

  // update ds
  spn_vk_ds_update_ttcks(instance, &device->environment, *ds);

  // bind
  spn_vk_ds_bind_render_ttcks(instance, cb, *ds);
}

//
//
//

void
spn_composition_pre_render_dispatch_indirect(struct spn_composition * const composition,
                                             VkCommandBuffer                cb)
{
  struct spn_composition_impl * const impl = composition->impl;

  VkDeviceSize const dbi_offset =
    impl->vk.ttcks.dbi.offset + SPN_VK_BUFFER_OFFSETOF(ttcks, ttcks, offsets_count);

  vkCmdDispatchIndirect(cb, impl->vk.ttcks.dbi.buffer, dbi_offset);
}

//
//
//

void
spn_composition_post_render(struct spn_composition * const composition)
{
  spn_composition_unlock_and_release(composition->impl);
}

//
//
//
