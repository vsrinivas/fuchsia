// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "render_impl.h"

#include "block_pool.h"
#include "common/vk/vk_assert.h"
#include "composition_impl.h"
#include "device.h"
#include "queue_pool.h"
#include "spinel.h"
#include "spinel_vk_types.h"
#include "spn_vk_target.h"
#include "styling_impl.h"

//
//
//

#include <string.h>

//
//
//

struct spn_render_submit_ext_base
{
  void *                       ext;
  spn_render_submit_ext_type_e type;
};

//
//
//

#define SPN_RI_MAX_SUBMIT_INFO_WAIT_COUNT 8

//
//
//

//
// A callback is only invoked if a H2D copy is required.
//

struct spn_ri_buffer_complete_payload
{
  struct spn_vk * instance;

  struct
  {
    struct spn_vk_ds_ttcks_t   ttcks;
    struct spn_vk_ds_styling_t styling;
    struct spn_vk_ds_surface_t surface;
  } ds;
};

static void
spn_ri_buffer_complete(void * pfn_payload)
{
  //
  // FENCE_POOL INVARIANT:
  //
  // COMPLETION ROUTINE MUST MAKE LOCAL COPIES OF PAYLOAD BEFORE ANY
  // POTENTIAL INVOCATION OF SPN_DEVICE_YIELD/WAIT/DRAIN()
  //
  struct spn_ri_buffer_complete_payload const * const payload  = pfn_payload;
  struct spn_vk * const                               instance = payload->instance;

  // release descriptor sets
  spn_vk_ds_release_ttcks(instance, payload->ds.ttcks);
  spn_vk_ds_release_styling(instance, payload->ds.styling);
  spn_vk_ds_release_surface(instance, payload->ds.surface);
}

//
//
//

spn_result
spn_ri_buffer(struct spn_device * const device, spn_render_submit_t const * const submit)
{
  struct spn_render_submit_ext_vk_buffer const * const ext = submit->ext;

  //
  // For now, we're capping the number of semaphores we can wait for
  // no reason other than it makes the code simpler and avoids use
  // of VLAs.
  //
  if (ext->si->waitSemaphoreCount > SPN_RI_MAX_SUBMIT_INFO_WAIT_COUNT)
    {
      return SPN_ERROR_RENDER_EXTENSION_VK_SUBMIT_INFO_WAIT_COUNT_EXCEEDED;
    }

  //
  // get a cb
  //
  VkCommandBuffer cb = spn_device_cb_acquire_begin(device);

  //
  // bind block_pool/composition/styling/surface
  //
  struct spn_vk * const instance = device->instance;

  //
  // DS: BLOCK POOL
  //
  spn_vk_ds_bind_render_block_pool(instance, cb, spn_device_block_pool_get_ds(device));

  //
  // DS: TTCKS
  //
  struct spn_vk_ds_ttcks_t ds_ttcks;

  spn_composition_impl_pre_render_ds(submit->composition, &ds_ttcks, cb);

  //
  // DS: STYLING
  //
  struct spn_vk_ds_styling_t ds_styling;

  spn_styling_impl_pre_render_ds(submit->styling, &ds_styling, cb);

  //
  // DS: SURFACE
  //

  // acquire surface descriptor set
  struct spn_vk_ds_surface_t ds_surface;

  spn_vk_ds_acquire_surface(instance, device, &ds_surface);

  // copy the dbi structs
  *spn_vk_ds_get_surface_surface(instance, ds_surface) = ext->surface;

  // update ds
  spn_vk_ds_update_surface(instance, device->environment, ds_surface);

  // bind ds
  spn_vk_ds_bind_render_surface(instance, cb, ds_surface);

  //
  // append push constants
  //
  struct spn_vk_push_render const push = {.tile_clip =
                                            {
                                              submit->tile_clip[0],
                                              submit->tile_clip[1],
                                              submit->tile_clip[2],
                                              submit->tile_clip[3],
                                            },
                                          .surface_pitch = ext->surface_pitch};

  spn_vk_p_push_render(instance, cb, &push);

  //
  // bind pipeline
  //
  spn_vk_p_bind_render(instance, cb);

  //
  // indirect dispatch the pipeline
  //
  spn_composition_impl_pre_render_dispatch(submit->composition, cb);

  //
  // end the cb and acquire a fence
  //
  struct spn_ri_buffer_complete_payload payload = {
    .instance = instance,
    .ds       = { .ttcks = ds_ttcks, .styling = ds_styling, .surface = ds_surface }
  };

  VkFence const fence =
    spn_device_cb_end_fence_acquire(device, cb, spn_ri_buffer_complete, &payload, sizeof(payload));
  //
  // acquire semaphores if still unsealed
  //
  uint32_t             waitSemaphoreCount = 0;
  VkSemaphore          pWaitSemaphores[SPN_RI_MAX_SUBMIT_INFO_WAIT_COUNT + 2];
  VkPipelineStageFlags pWaitDstStageMask[SPN_RI_MAX_SUBMIT_INFO_WAIT_COUNT + 2];

  spn_composition_impl_pre_render_wait(submit->composition,
                                       &waitSemaphoreCount,
                                       pWaitSemaphores,
                                       pWaitDstStageMask);

  spn_styling_impl_pre_render_wait(submit->styling,
                                   &waitSemaphoreCount,
                                   pWaitSemaphores,
                                   pWaitDstStageMask);

  //
  // if there are semaphores we need to wait on then we need to create
  // a new submit info
  //
  if (waitSemaphoreCount == 0)
    {
      vk(QueueSubmit(spn_device_queue_next(device), 1, ext->si, fence));
    }
  else
    {
      uint32_t const count = ext->si->waitSemaphoreCount;

      memcpy(pWaitSemaphores + waitSemaphoreCount,
             ext->si->pWaitSemaphores,
             count * sizeof(*pWaitSemaphores));
      memcpy(pWaitDstStageMask + waitSemaphoreCount,
             ext->si->pWaitDstStageMask,
             count * sizeof(*pWaitDstStageMask));

      waitSemaphoreCount += count;

      struct VkSubmitInfo si = *ext->si;

      si.waitSemaphoreCount = waitSemaphoreCount;
      si.pWaitSemaphores    = pWaitSemaphores;
      si.pWaitDstStageMask  = pWaitDstStageMask;

      vk(QueueSubmit(spn_device_queue_next(device), 1, &si, fence));
    }

  return SPN_SUCCESS;
}

//
//
//

spn_result
spn_render_impl(struct spn_device * const device, spn_render_submit_t const * const submit)
{
  spn_result result;

  result = spn_composition_seal(submit->composition);

  if (result)
    {
      return result;
    }

  result = spn_styling_seal(submit->styling);

  if (result)
    {
      return result;
    }

  //
  // walk the extension chain
  //
  struct spn_render_submit_ext_base const * const ext = submit->ext;

  if (ext == NULL)
    {
      return SPN_ERROR_RENDER_EXTENSION_INVALID;
    }

  switch (ext->type)
    {
      case SPN_RENDER_SUBMIT_EXT_TYPE_VK_BUFFER:
        return spn_ri_buffer(device, submit);

      case SPN_RENDER_SUBMIT_EXT_TYPE_VK_IMAGE:
      default:
        return SPN_ERROR_NOT_IMPLEMENTED;
    }
}

//
//
//
