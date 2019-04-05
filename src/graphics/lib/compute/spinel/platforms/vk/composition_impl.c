// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "composition_impl.h"
#include "device.h"
#include "ring.h"
#include "target.h"
#include "core_c.h"
#include "state_assert.h"
#include "handle_pool.h"
#include "queue_pool.h"
#include "block_pool.h"
#include "semaphore_pool.h"
#include "queue_pool.h"

#include "common/vk/vk_assert.h"
#include "common/vk/vk_barrier.h"

//
// composition states
//

typedef enum spn_ci_state_e {

  SPN_CI_STATE_RESETTING, // unsealed, but waiting for reset to complete
  SPN_CI_STATE_UNSEALED,  // ready to place rasters
  SPN_CI_STATE_SEALING,   // waiting for PLACE and TTCK_SORT
  SPN_CI_STATE_SEALED     // sort & segment complete

} spn_ci_state_e;

//
// There are always as many dispatch records as there are fences in
// the fence pool.  This simplifies reasoning about concurrency.
//
// The dispatch record in the composition tracks resources associated
// with wip and in-flight PLACE submissions.
//

struct spn_ci_dispatch
{
  struct {
    uint32_t    span;
    uint32_t    head;
  } cp;

  struct {
    VkSemaphore place;
  } semaphore;

  bool          unreleased;
};

//
//
//

struct spn_ci_vk
{
  struct {
    struct {
      VkDescriptorBufferInfo  dbi;
      VkDeviceMemory          dm;
    } h;

    struct {
      VkDescriptorBufferInfo  dbi;
      VkDeviceMemory          dm;
    } d;
  } rings;

  struct {
    VkDescriptorBufferInfo    dbi;
    VkDeviceMemory            dm;
  } ttcks;

  struct {
    VkDescriptorBufferInfo    dbi;
    VkDeviceMemory            dm;
  } copyback;

  struct {
    VkSemaphore               resetting;
    VkSemaphore               sealing;
  } semaphore;
};

//
//
//

struct spn_ci_copyback
{
  uint32_t ttcks;
  uint32_t offsets;
};

//
//
//

struct spn_composition_impl
{
  struct spn_composition         * composition;

  struct spn_device              * device;

  struct spn_target_config const * config;

  struct spn_ci_vk                 vk;

  //
  // mapped command ring and copyback counts
  //
  struct {
    struct {
      struct spn_cmd_place       * extent;
      struct spn_ring              ring;
    } cp; // place commands

    struct {
      struct spn_ci_copyback     * extent;
    } cb;
  } mapped;

  //
  // records of work-in-progress and work-in-flight
  //
  struct {
    struct spn_ci_dispatch       * extent;
    struct spn_ring                ring;
  } dispatches;

  //
  // scratchpad for semaphores instead of using C99/VLA
  //
  struct {
    VkSemaphore                  * semaphores;
    VkPipelineStageFlags         * psfs;
    uint32_t                       count;
  } place;

  //
  // all rasters are retained until reset or release
  //
  struct {
    spn_handle_t                 * extent;
    uint32_t                       count;
    uint32_t                       size;
  } rasters;

  SPN_ASSERT_STATE_DECLARE(spn_ci_state_e);

  uint32_t                         lock_count; // # of wip renders
};

//
// A dispatch captures how many paths and blocks are in a dispatched or
// the work-in-progress compute grid.
//

static
struct spn_ci_dispatch *
spn_ci_dispatch_idx(struct spn_composition_impl * const impl,
                    uint32_t                      const idx)
{
  return impl->dispatches.extent + idx;
}

static
struct spn_ci_dispatch *
spn_ci_dispatch_head(struct spn_composition_impl * const impl)
{
  return spn_ci_dispatch_idx(impl,impl->dispatches.ring.head);
}

static
struct spn_ci_dispatch *
spn_ci_dispatch_tail(struct spn_composition_impl * const impl)
{
  return spn_ci_dispatch_idx(impl,impl->dispatches.ring.tail);
}

//
//
//

static
bool
spn_ci_dispatch_is_empty(struct spn_ci_dispatch const  * const dispatch)
{
  return dispatch->cp.span == 0;
}

static
void
spn_ci_dispatch_init(struct spn_composition_impl * const impl,
                     struct spn_ci_dispatch      * const dispatch)
{
  dispatch->cp.span    = 0;
  dispatch->cp.head    = impl->mapped.cp.ring.head;

  // don't care about semaphore

  dispatch->unreleased = false;
}

static
void
spn_ci_dispatch_drop(struct spn_composition_impl * const impl)
{
  struct spn_ring * const ring = &impl->dispatches.ring;

  spn_ring_drop_1(ring);

  while (spn_ring_is_empty(ring)) {
    spn_device_wait(impl->device);
  }

  struct spn_ci_dispatch * const dispatch = spn_ci_dispatch_idx(impl,ring->head);

  spn_ci_dispatch_init(impl,dispatch);
}

//
// We are avoiding use of VLA/alloca() but need to provide pipeline
// stage flags along with the semaphore wait list.
//

static
void
spn_ci_psfs_init(struct spn_composition_impl * const impl)
{
  uint32_t               const size = impl->dispatches.ring.size;
  VkPipelineStageFlags * const psfs = impl->place.psfs;

  for (uint32_t ii=0; ii<size; ii++)
    {
      psfs[ii] = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
}

//
// Gather the semaphores for all in-flight PLACE dispatches.
//

static
void
spn_ci_gather_place_semaphores(struct spn_composition_impl * const impl)
{
  struct spn_ring * const ring = &impl->dispatches.ring;
  uint32_t                rem  = ring->rem;

  if (rem == 0) {
    return;
  }

  uint32_t                             tail       = ring->tail;
  uint32_t                       const size       = ring->size;
  struct spn_ci_dispatch const * const dispatches = impl->dispatches.extent;

  VkSemaphore                  * const semaphores = impl->place.semaphores;
  uint32_t                             count      = 0;

  while (true)
    {
      VkSemaphore const sp = dispatches[tail++].semaphore.place;

      if (sp != VK_NULL_HANDLE) {
        semaphores[count++] = sp;
      }

      if (--rem == 0)
        break;

      if (tail == size)
        tail = 0;
    }

  impl->place.count = count;
}

//
// COMPLETION: PLACE
//

struct spn_ci_complete_payload_1
{
  struct spn_composition_impl *  impl;

  struct {
    struct spn_target_ds_ttcks_t ttcks;
    struct spn_target_ds_place_t place;
  } ds;

  uint32_t                       dispatch_idx; // dispatch idx
};

//
// COMPLETION: INDIRECT SORT
//

struct spn_ci_complete_payload_2
{
  struct spn_composition_impl *  impl;

  struct {
    struct spn_target_ds_ttcks_t ttcks;
  } ds;

  struct {
    VkSemaphore                  sort;
  } semaphore;
};


//
// COMPLETION: MERGE & SEGMENT
//

struct spn_ci_complete_payload_3
{
  struct spn_composition_impl *  impl;

  struct {
    struct spn_target_ds_ttcks_t ttcks;
  } ds;
};

//
//
//

static
void
spn_ci_complete_p_1(void * pfn_payload)
{
  //
  // FENCE_POOL INVARIANT:
  //
  // COMPLETION ROUTINE MUST MAKE LOCAL COPIES OF PAYLOAD BEFORE ANY
  // POTENTIAL INVOCATION OF SPN_DEVICE_YIELD/WAIT/DRAIN()
  //
  struct spn_ci_complete_payload_1 const * const payload = pfn_payload;
  struct spn_composition_impl            * const impl    = payload->impl;
  struct spn_device                      * const device  = impl->device;
  struct spn_target                      * const target  = device->target;

  // release descriptor sets
  spn_target_ds_release_ttcks(target,payload->ds.ttcks);
  spn_target_ds_release_place(target,payload->ds.place);

  //
  // If the dispatch is the tail of the ring then try to release as
  // many dispatch records as possible...
  //
  // Note that kernels can complete in any order so the release
  // records need to add to the mapped.ring.tail in order.
  //
  uint32_t const           dispatch_idx = payload->dispatch_idx;
  struct spn_ci_dispatch * dispatch     = spn_ci_dispatch_idx(impl,dispatch_idx);

  // immediately release the semaphore
  spn_device_semaphore_pool_release(device,dispatch->semaphore.place);

  // implies dispatch is complete
  dispatch->semaphore.place = VK_NULL_HANDLE;

  if (spn_ring_is_tail(&impl->dispatches.ring,dispatch_idx))
    {
      do {

        spn_ring_release_n(&impl->mapped.cp.ring,dispatch->cp.span);
        spn_ring_release_n(&impl->dispatches.ring,1);

        dispatch->unreleased = false;
        dispatch             = spn_ci_dispatch_tail(impl);

      } while (dispatch->unreleased);
    }
  else
    {
      dispatch->unreleased = true;
    }
}

//
//
//

static
void
spn_ci_flush(struct spn_composition_impl * const impl)
{
  struct spn_ci_dispatch * const dispatch = spn_ci_dispatch_head(impl);

  // anything to launch?
  if (spn_ci_dispatch_is_empty(dispatch)) {
    return;
  }

  //
  // We're go for launch...
  //
  struct spn_device * const device = impl->device;
  struct spn_target * const target = device->target;

  // get a cb
  VkCommandBuffer cb = spn_device_cb_acquire_begin(device);

  //
  // BLOCK POOL
  //
  // bind global BLOCK_POOL descriptor set
  spn_target_ds_bind_place_block_pool(target,cb,spn_device_block_pool_get_ds(device));

  //
  // TTCKS
  //
  // acquire TTCKS descriptor set
  struct spn_target_ds_ttcks_t ds_ttcks;

  spn_target_ds_acquire_ttcks(target,device,&ds_ttcks);

  // copy the dbi structs
  *spn_target_ds_get_ttcks_ttcks(target,ds_ttcks) = impl->vk.ttcks.dbi;

  // update TTCKS descriptor set
  spn_target_ds_update_ttcks(target,device->vk,ds_ttcks);

  // bind the TTCKS descriptor set
  spn_target_ds_bind_place_ttcks(target,cb,ds_ttcks);

  //
  // PLACE
  //
  // acquire PLACE descriptor set
  struct spn_target_ds_place_t ds_place;

  spn_target_ds_acquire_place(target,device,&ds_place);

  // copy the dbi struct
  *spn_target_ds_get_place_place(target,ds_place) = impl->vk.rings.d.dbi;

  // update PLACE descriptor set
  spn_target_ds_update_place(target,device->vk,ds_place);

  // bind PLACE descriptor set
  spn_target_ds_bind_place_place(target,cb,ds_place);

  //
  // Set up push constants -- note that for now the paths_copy push
  // constants are an extension of the paths_alloc constants.
  //
  // This means we can push the constants once.
  //
  struct spn_target_push_place const push =
    {
      .place_clip = { 0, 0, INT32_MAX, INT32_MAX }
    };

  spn_target_p_push_place(target,cb,&push);

  // bind PLACE pipeline
  spn_target_p_bind_place(target,cb);

  // dispatch the pipeline
  vkCmdDispatch(cb,dispatch->cp.span,1,1);

  //
  // submit the command buffer
  //
  struct spn_ci_complete_payload_1 p_1 =
    {
      .impl         = impl,
      .ds = {
        .ttcks.idx  = ds_ttcks.idx,
        .place.idx  = ds_place.idx
      },
      .dispatch_idx = impl->dispatches.ring.head,
    };

  dispatch->semaphore.place = spn_device_semaphore_pool_acquire(device);

  VkFence const fence = spn_device_cb_end_fence_acquire(device,
                                                        cb,
                                                        spn_ci_complete_p_1,
                                                        &p_1,
                                                        sizeof(p_1));
  // boilerplate submit
  struct VkSubmitInfo const si = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = NULL,
    .waitSemaphoreCount   = 0,
    .pWaitSemaphores      = NULL,
    .pWaitDstStageMask    = NULL,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores    = &dispatch->semaphore.place
  };

  vk(QueueSubmit(spn_device_queue_next(device),1,&si,fence));

  //
  // the current dispatch is now "in flight" so drop it and try to
  // acquire and initialize the next
  //
  spn_ci_dispatch_drop(impl);
}

//
//
//

#if 0

static
void
spn_ci_init(struct spn_composition_impl * const impl)
{
  ;
}

//
//
//

static
void
spn_ci_reset_when_unsealed(struct spn_composition_impl * const impl)
{
  impl->reset_when_unsealed = true;
}

static
void
spn_ci_reset_while_unsealed(struct spn_composition_impl * const impl)
{
  impl->reset_when_unsealed = false;

  spn_ci_init(impl);
}

//
//
//

static
void
spn_ci_seal_after_unsealed(struct spn_composition_impl * const impl)
{
  // nothing has changed so relock
  impl->state = SPN_CI_STATE_SEALED;
}

//
//
//

static
void
spn_ci_unseal_after_sealed(struct spn_composition_impl * const impl)
{
  // nothing has changed
  impl->state = SPN_CI_STATE_UNSEALED;
}

//
//
//

static
void
spn_ci_unseal_after_sealed_and_reset(struct spn_composition_impl * const impl)
{
  // nothing has changed
  impl->state = SPN_CI_STATE_UNSEALED;

  spn_ci_reset_while_unsealed(impl);
}

//
//
//

static
void
spn_ci_block_until_unsealed_and_reseal(struct spn_composition_impl * const impl)
{
#if 0
  if (impl->reset_when_unsealed)
    {
      spn_ci_reset_while_unsealed(impl);
    }
#endif
}

#endif

//
//
//

static
void
spn_ci_complete_p_3(void * pfn_payload)
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
  struct spn_ci_complete_payload_3 const * const p_3    = pfn_payload;
  struct spn_composition_impl            * const impl   = p_3->impl;
  struct spn_device                      * const device = impl->device;
  struct spn_target                      * const target = device->target;

  // release the ttcks ds -- will never wait()
  spn_target_ds_release_ttcks(target,p_3->ds.ttcks); // FIXME -- reuse

  // release the sealing semaphore
  spn_device_semaphore_pool_release(device,impl->vk.semaphore.sealing);

  // move to sealed state
  impl->state = SPN_CI_STATE_SEALED;
}

//
//
//

static
void
spn_ci_complete_p_2(void * pfn_payload)
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
  struct spn_ci_complete_payload_2 const * const p_2    = pfn_payload;
  struct spn_composition_impl            * const impl   = p_2->impl;
  struct spn_device                      * const device = impl->device;
  struct spn_target                      * const target = device->target;

  // release the copy semaphore
  spn_device_semaphore_pool_release(device,p_2->semaphore.sort);

  //
  // PHASE 3:
  //

  // launch sort dependent upon in-flight PLACE invocations
  VkCommandBuffer cb = spn_device_cb_acquire_begin(device);

  //
  // DS: TTCKS
  //
  spn_target_ds_bind_segment_ttck_ttcks(target,cb,p_2->ds.ttcks);

#if 0
  hs_vk_merge();
#endif

  vk_barrier_compute_w_to_compute_r(cb);

  ////////////////////////////////////////////////////////////////
  //
  // SHADER: TTCK_SEGMENT
  //
  ////////////////////////////////////////////////////////////////

  // bind the pipeline
  spn_target_p_bind_segment_ttck(target,cb);

#if 0
  // dispatch one workgroup per fill command
  vkCmdDispatch(cb,count_ru/slab_size,1,1);
#endif

  // signal completion with a semaphore
}

//
//
//

static
void
spn_ci_unsealed_to_sealing(struct spn_composition_impl * const impl)
{
  //
  struct spn_device * const device = impl->device;
  struct spn_target * const target = device->target;

  // semaphore will be signaled once segmenting is complete
  impl->vk.semaphore.sealing = spn_device_semaphore_pool_acquire(device);

  // flush current dispatch
  spn_ci_flush(impl);

  // launch sort dependent upon in-flight PLACE invocations
  VkCommandBuffer cb = spn_device_cb_acquire_begin(device);

  //
  // COPYBACK
  //
  VkBufferCopy const bc = {
    .srcOffset = SPN_TARGET_BUFFER_OFFSETOF(ttcks,ttcks,ttcks_count),
    .dstOffset = OFFSET_OF_MACRO(struct spn_ci_copyback,ttcks),
    .size      = sizeof(impl->mapped.cb.extent->ttcks)
  };

  VkBuffer ttcks = impl->vk.ttcks.dbi.buffer;

  vkCmdCopyBuffer(cb,
                  ttcks,
                  impl->vk.copyback.dbi.buffer,
                  1,
                  &bc);

  //
  // TTCKS
  //

  // acquire TTCKS descriptor set
  struct spn_target_ds_ttcks_t ds_ttcks;

  spn_target_ds_acquire_ttcks(target,device,&ds_ttcks);

  // copy the dbi structs
  *spn_target_ds_get_ttcks_ttcks(target,ds_ttcks) = impl->vk.ttcks.dbi;

  // update TTCKS descriptor set
  spn_target_ds_update_ttcks(target,device->vk,ds_ttcks);

  // bind the TTCKS descriptor set
  spn_target_ds_bind_segment_ttck_ttcks(target,cb,ds_ttcks);

  //
  // SORT
  //
#if 0
  hs_vk_sort_indirect(cb,
                      ttcks,
                      SPN_TARGET_BUFFER_OFFSETOF(ttcks,ttcks,ttcks),
                      SPN_TARGET_BUFFER_OFFSETOF(ttcks,ttcks,ttcks_count));
#endif

  //
  //
  //
  struct spn_ci_complete_payload_2 p_2 =
    {
      .impl          = impl,
      .ds = {
        .ttcks.idx   = ds_ttcks.idx,
      },
      .semaphore = {
        .sort        = spn_device_semaphore_pool_acquire(device)
      }
    };

  // may wait()
  VkFence const fence = spn_device_cb_end_fence_acquire(device,
                                                        cb,
                                                        spn_ci_complete_p_2,
                                                        &p_2,
                                                        sizeof(p_2));

  // delay gathering PLACE semaphores until the last moment
  spn_ci_gather_place_semaphores(impl);

  // boilerplate submit
  struct VkSubmitInfo const si = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = NULL,
    .waitSemaphoreCount   = impl->place.count,
    .pWaitSemaphores      = impl->place.semaphores,
    .pWaitDstStageMask    = impl->place.psfs,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores    = &p_2.semaphore.sort
  };

  vk(QueueSubmit(spn_device_queue_next(device),1,&si,fence));
}

//
//
//

static
void
spn_ci_resetting_reset(struct spn_composition_impl * const impl)
{
  // double reset simply resets host-side counters
}

static
void
spn_ci_unsealed_reset(struct spn_composition_impl * const impl)
{
  ;
}

//
//
//

static
void
spn_ci_block_until_sealed(struct spn_composition_impl * const impl)
{
  struct spn_device * const device = impl->device;

  while (impl->state != SPN_CI_STATE_SEALED) {
    spn_device_wait(device);
  }
}

static
void
spn_ci_sealed_unseal(struct spn_composition_impl * const impl)
{
  //
  // wait for any in-flight renders to complete
  //
  struct spn_device * const device = impl->device;

  while (impl->lock_count > 0) {
    spn_device_wait(device);
  }

  impl->state = SPN_CI_STATE_UNSEALED;
}

//
//
//

static
spn_result
spn_ci_seal(struct spn_composition_impl * const impl)
{
  switch (impl->state)
    {
    case SPN_CI_STATE_RESETTING:
    case SPN_CI_STATE_UNSEALED:
      spn_ci_unsealed_to_sealing(impl);
      return SPN_SUCCESS;

    case SPN_CI_STATE_SEALING:
      return SPN_SUCCESS;

    case SPN_CI_STATE_SEALED:
    default:
      return SPN_SUCCESS;
    }
}


static
spn_result
spn_ci_unseal(struct spn_composition_impl * const impl)
{
  switch (impl->state)
    {
    case SPN_CI_STATE_RESETTING:
      return SPN_SUCCESS;

    case SPN_CI_STATE_UNSEALED:
      return SPN_SUCCESS;

    case SPN_CI_STATE_SEALING:
      spn_ci_block_until_sealed(impl);
      // fall-through
    case SPN_CI_STATE_SEALED:
    default:
      spn_ci_sealed_unseal(impl);
      return SPN_SUCCESS;
    }
}

static
spn_result
spn_ci_reset(struct spn_composition_impl * const impl)
{
  switch (impl->state)
    {
    case SPN_CI_STATE_RESETTING:
      spn_ci_resetting_reset(impl); // double-reset
      return SPN_SUCCESS;

    case SPN_CI_STATE_UNSEALED:
      spn_ci_unsealed_reset(impl);
      return SPN_SUCCESS;

    case SPN_CI_STATE_SEALING:
      return SPN_ERROR_RASTER_BUILDER_SEALED;

    case SPN_CI_STATE_SEALED:
    default:
      return SPN_ERROR_RASTER_BUILDER_SEALED;
    }
}

//
//
//

static
spn_result
spn_ci_clone(struct spn_composition_impl * const impl, struct spn_composition * * const clone)
{
  return SPN_ERROR_NOT_IMPLEMENTED;
}

//
//
//

static
spn_result
spn_ci_get_bounds(struct spn_composition_impl * const impl, int32_t bounds[4])
{
  return SPN_ERROR_NOT_IMPLEMENTED;
}

//
//
//

static
spn_result
spn_ci_place(struct spn_composition_impl * const impl,
             spn_raster_t         const  *       rasters,
             spn_layer_id         const  *       layer_ids,
             int32_t              const (*       txtys)[2],
             uint32_t                            count)
{
  switch (impl->state)
    {
    case SPN_CI_STATE_RESETTING:
    case SPN_CI_STATE_UNSEALED:
      break;

    case SPN_CI_STATE_SEALING:
    case SPN_CI_STATE_SEALED:
    default:
      return SPN_ERROR_RASTER_BUILDER_SEALED;
    }

  // nothing to do?
  if (count == 0) {
    return SPN_SUCCESS;
  }

  // validate there is enough room for rasters
  if (impl->rasters.count + count > impl->rasters.size) {
    return SPN_ERROR_RASTER_BUILDER_TOO_MANY_RASTERS;
  }

#if 0
  //
  // FIXME -- No, we should NEVER need to do this.  The layer invoking
  // Spinel should ensure that layer ids remain in range.  Do not
  // enable this.
  //
  // validate layer ids
  //
  for (uint_32_t ii=0; ii<count; ii++) {
    if (layer_ids[ii] > SPN_TTCK_LAYER_MAX) {
      return SPN_ERROR_LAYER_ID_INVALID;
    }
  }
#endif

  //
  // validate and retain all rasters
  //
  struct spn_device * const device = impl->device;

  {
    spn_result const err = spn_device_handle_pool_validate_retain_h_rasters(device,
                                                                            rasters,
                                                                            count);
    if (err)
      return err;
  }

  //
  // No survivable errors from here onward... any failure beyond here
  // will be fatal to the context -- most likely too many ttcks.
  //

#if 0
  //
  // block if resetting...
  //
  while (impl->state != SPN_CI_STATE_UNSEALED) {
    spn_device_wait(device);
  }

  //
  // FIXME -- use the vk.semaphore.resetting before submitting a place
  //

#endif

  //
  // save the untyped raster handles
  //
  spn_raster_t * saved = impl->rasters.extent + impl->rasters.count;

  impl->rasters.count += count;

  for (uint32_t ii=0; ii<count; ii++) {
    saved[ii] = SPN_TYPED_HANDLE_TO_HANDLE(rasters[ii]);
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
      uint32_t avail = MIN_MACRO(uint32_t,count,spn_ring_rem_nowrap(ring));

      //
      // if ring is full then this implies we're already waiting on
      // dispatches because an eager launch would've occurred
      //
      if (avail == 0)
        {
          spn_device_wait(device);

          continue;
        }

      //
      // increment dispatch span
      //
      struct spn_ci_dispatch * const dispatch = spn_ci_dispatch_head(impl);

      dispatch->cp.span += avail;

      //
      // otherwise, append commands
      //
      struct spn_cmd_place * cmds = impl->mapped.cp.extent + impl->mapped.cp.ring.head;

      spn_ring_drop_n(ring,avail);

      count -= avail;

      while (avail-- > 0)
        {
          *cmds++ = (struct spn_cmd_place)
            {
              .raster_h = *rasters++,
              .layer_id = *layer_ids++,
              .txty     = { *(*txtys++) }
            };
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
      if (count == 0) {
        return SPN_SUCCESS;
      }
    }
}

//
//
//

static
spn_result
spn_ci_release(struct spn_composition_impl * const impl)
{
  //
  // was this the last reference?
  //
  if (--impl->composition->ref_count != 0) {
    return SPN_SUCCESS;
  }

  struct spn_device * const device = impl->device;

  //
  // wait for any in-flight PLACE dispatches to complete
  //
  while (!spn_ring_is_empty(&impl->dispatches.ring)) {
    spn_device_wait(device);
  }

  //
  // wait for any in-flight renders to complete
  //
  while (impl->lock_count > 0) {
    spn_device_wait(device);
  }

  //
  // release retained rasters
  //
  spn_device_handle_pool_release_d_rasters(impl->device,
                                           impl->rasters.extent,
                                           impl->rasters.count);

  //
  // Note that we don't have to unmap before freeing
  //

  //
  // free copyback
  //
  spn_allocator_device_perm_free(&device->allocator.device.perm.copyback,
                                 device->vk,
                                 &impl->vk.copyback.dbi,
                                 impl->vk.copyback.dm);
  //
  // free ttcks
  //
  spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                 device->vk,
                                 &impl->vk.ttcks.dbi,
                                 impl->vk.ttcks.dm);
  //
  // free ring
  //
  if (impl->config->composition.vk.rings.d != 0)
    {
      spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                     device->vk,
                                     &impl->vk.rings.d.dbi,
                                     impl->vk.rings.d.dm);
    }

  spn_allocator_device_perm_free(&device->allocator.device.perm.coherent,
                                 device->vk,
                                 &impl->vk.rings.h.dbi,
                                 impl->vk.rings.h.dm);
  //
  // free host allocations
  //
  struct spn_allocator_host_perm * const perm = &impl->device->allocator.host.perm;

  spn_allocator_host_perm_free(perm,impl->dispatches.extent);
  spn_allocator_host_perm_free(perm,impl->composition);
  spn_allocator_host_perm_free(perm,impl);

  return SPN_SUCCESS;
}

//
//
//

static
void
spn_ci_retain_and_lock(struct spn_composition_impl * const impl)
{
  impl->composition->ref_count += 1;

  impl->lock_count             += 1;
}

static
void
spn_composition_unlock_and_release(struct spn_composition_impl * const impl)
{
  impl->lock_count -= 1;

  spn_ci_release(impl);
}

//
//
//

spn_result
spn_composition_impl_create(struct spn_device        * const device,
                            struct spn_composition * * const composition)
{
  //
  // retain the context
  // spn_context_retain(context);
  //
  struct spn_allocator_host_perm * const perm = &device->allocator.host.perm;

  //
  // allocate impl
  //
  struct spn_composition_impl * const impl =
    spn_allocator_host_perm_alloc(perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  sizeof(*impl));
  //
  // allocate composition
  //
  struct spn_composition * const c =
    spn_allocator_host_perm_alloc(perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  sizeof(*c));

  // init impl and pb back-pointers
  *composition      = c;
  impl->composition = c;
  c->impl           = impl;

  // save device
  impl->device      = device;

  struct spn_target_config const * const config = spn_target_get_config(device->target);

  impl->config      = config;

  impl->lock_count  = 0;

  //
  // initialize composition
  //
  c->release       = spn_ci_release;
  c->seal          = spn_ci_seal;
  c->unseal        = spn_ci_unseal;
  c->reset         = spn_ci_reset;
  c->clone         = spn_ci_clone;
  c->get_bounds    = spn_ci_get_bounds;
  c->place         = spn_ci_place;

  c->ref_count     = 1;

  // the composition impl starts out unsealed
  SPN_ASSERT_STATE_INIT(impl,SPN_CI_STATE_UNSEALED);

  //
  // allocate and map ring
  //
  size_t const ring_size = config->composition.size.ring * sizeof(*impl->mapped.cp.extent);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.coherent,
                                  device->vk,
                                  ring_size,
                                  NULL,
                                  &impl->vk.rings.h.dbi,
                                  &impl->vk.rings.h.dm);

  vk(MapMemory(device->vk->d,
               impl->vk.rings.h.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void**)&impl->mapped.cp.extent));

  if (config->composition.vk.rings.d != 0)
    {
      spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                      device->vk,
                                      ring_size,
                                      NULL,
                                      &impl->vk.rings.d.dbi,
                                      &impl->vk.rings.d.dm);
    }
  else
    {
      impl->vk.rings.d.dbi = (VkDescriptorBufferInfo){ .buffer = VK_NULL_HANDLE, .offset = 0, .range = 0 };
      impl->vk.rings.d.dm  = VK_NULL_HANDLE;
    }

  //
  // allocate ttck descriptor
  //
  size_t const ttcks_size =
    SPN_TARGET_BUFFER_OFFSETOF(ttcks,ttcks,ttcks) +
    config->composition.size.ttcks * sizeof(SPN_TYPE_UVEC2);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                  device->vk,
                                  ttcks_size,
                                  NULL,
                                  &impl->vk.ttcks.dbi,
                                  &impl->vk.ttcks.dm);

  //
  // allocate and map copyback
  //
  size_t const copyback_size = sizeof(*impl->mapped.cb.extent);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.copyback,
                                  device->vk,
                                  copyback_size,
                                  NULL,
                                  &impl->vk.copyback.dbi,
                                  &impl->vk.copyback.dm);

  vk(MapMemory(device->vk->d,
               impl->vk.copyback.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void**)&impl->mapped.cb.extent));

  //
  // allocate release resources
  //

  uint32_t const max_in_flight   = config->fence_pool.size;

  size_t   const dispatches_size = max_in_flight                    * sizeof(*impl->dispatches.extent);
  size_t   const semaphores_size = max_in_flight                    * sizeof(*impl->place.semaphores);
  size_t   const psfs_size       = max_in_flight                    * sizeof(*impl->place.psfs);
  size_t   const rasters_size    = config->composition.size.rasters * sizeof(*impl->rasters.extent);

  size_t   const h_extent_size   = dispatches_size + semaphores_size + psfs_size + rasters_size;

  impl->dispatches.extent        = spn_allocator_host_perm_alloc(perm,
                                                                 SPN_MEM_FLAGS_READ_WRITE,
                                                                 h_extent_size);

  impl->place.semaphores         = (void*)(impl->dispatches.extent + max_in_flight);
  impl->place.psfs               = (void*)(impl->place.semaphores  + max_in_flight);

  impl->rasters.extent           = (void*)(impl->place.psfs        + max_in_flight);
  impl->rasters.size             = config->composition.size.rasters;

  spn_ring_init(&impl->dispatches.ring,max_in_flight);

  spn_ci_dispatch_init(impl,impl->dispatches.extent);

  spn_ci_psfs_init(impl);

  return SPN_SUCCESS;
}

//
//
//

void
spn_composition_impl_pre_render_ds(struct spn_composition       * const composition,
                                   struct spn_target_ds_ttcks_t * const ds,
                                   VkCommandBuffer                      cb)
{
  struct spn_composition_impl * const impl   = composition->impl;
  struct spn_device           * const device = impl->device;
  struct spn_target           * const target = device->target;

  assert(impl->state >= SPN_CI_STATE_SEALING);

  //
  // acquire TTCKS descriptor set
  //
  spn_target_ds_acquire_ttcks(target,device,ds);

  // copy the dbi structs
  *spn_target_ds_get_ttcks_ttcks(target,*ds) = impl->vk.ttcks.dbi;

  // update ds
  spn_target_ds_update_ttcks(target,device->vk,*ds);

  // bind
  spn_target_ds_bind_render_ttcks(target,cb,*ds);
}

//
//
//

void
spn_composition_impl_pre_render_dispatch(struct spn_composition * const composition,
                                         VkCommandBuffer                cb)
{
  struct spn_composition_impl * const impl = composition->impl;

  vkCmdDispatchIndirect(cb,
                        impl->vk.ttcks.dbi.buffer,
                        SPN_TARGET_BUFFER_OFFSETOF(ttcks,ttcks,offsets_count));
}

//
//
//

void
spn_composition_impl_pre_render_wait(struct spn_composition * const composition,
                                     uint32_t               * const waitSemaphoreCount,
                                     VkSemaphore            * const pWaitSemaphores,
                                     VkPipelineStageFlags   * const pWaitDstStageMask)
{
  struct spn_composition_impl * const impl = composition->impl;

  assert(impl->state >= SPN_CI_STATE_SEALING);

  if (impl->state == SPN_CI_STATE_SEALING)
    {
      uint32_t const idx = (*waitSemaphoreCount)++;

      pWaitSemaphores  [idx] = impl->vk.semaphore.sealing;
      pWaitDstStageMask[idx] = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
}

//
//
//
