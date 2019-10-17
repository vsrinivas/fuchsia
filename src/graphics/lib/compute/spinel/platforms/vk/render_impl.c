// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "render_impl.h"

#include "block_pool.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "composition_impl.h"
#include "device.h"
#include "dispatch.h"
#include "queue_pool.h"
#include "spinel.h"
#include "spinel_assert.h"
#include "spinel_vk_types.h"
#include "styling_impl.h"
#include "vk_target.h"

//
//
//

#include <stdio.h>
#include <string.h>

//
// Used to probe the type
//

struct spn_render_submit_ext_base
{
  void *                       ext;
  spn_render_submit_ext_type_e type;
};

//
// A callback is only invoked if a H2D copy is required.
//

struct spn_ri_complete_payload
{
  struct spn_device *      device;
  struct spn_composition * composition;
  struct spn_styling *     styling;

  struct
  {
    struct spn_vk_ds_ttcks_t   ttcks;
    struct spn_vk_ds_styling_t styling;
    struct spn_vk_ds_surface_t surface;
  } ds;
};

//
//
//

static void
spn_ri_complete(void * pfn_payload)
{
  struct spn_ri_complete_payload const * const payload  = pfn_payload;
  struct spn_vk * const                        instance = payload->device->instance;

  // release descriptor sets
  spn_vk_ds_release_ttcks(instance, payload->ds.ttcks);
  spn_vk_ds_release_styling(instance, payload->ds.styling);
  spn_vk_ds_release_surface(instance, payload->ds.surface);

  // release locks on composition and styling
  spn_composition_post_render(payload->composition);
  spn_styling_post_render(payload->styling);
}

//
// DEBUG: remove when bootstrapping is complete
//

static void
spn_ri_debug_buffer_copy_buffer_to_buffer(
  struct spn_render_submit_ext_vk_copy_buffer_to_buffer const * const ext,
  VkDescriptorBufferInfo const * const                                src,
  VkCommandBuffer                                                     cb)
{
  //
  // copy the surface to a buffer
  //
  vk_barrier_compute_w_to_transfer_r(cb);

  VkDeviceSize const ranges = MIN_MACRO(VkDeviceSize, src->range, ext->dst.range);
  VkDeviceSize const size   = MIN_MACRO(VkDeviceSize, ranges, ext->dst_size);

  VkBufferCopy const copy[] = {
    { .srcOffset = src->offset, .dstOffset = ext->dst.offset, .size = size }
  };

  vkCmdCopyBuffer(cb, src->buffer, ext->dst.buffer, 1, copy);

  //
  // FIXME(allanmac): verify whether this is necessary with host
  // coherent memory.
  //
  // NOTE(allanmac): this assumes we're copying to a mapped buffer
  //
  // make the copyback visible to the host
  vk_barrier_transfer_w_to_host_r(cb);
}

static void
spn_ri_debug_buffer_copy_buffer_to_image(
  struct spn_render_submit_ext_vk_copy_buffer_to_image const * const ext,
  VkDescriptorBufferInfo const * const                               src,
  VkCommandBuffer                                                    cb)
{
  //
  // copy the surface to an image
  //
  vk_barrier_compute_w_to_transfer_r(cb);

  vkCmdCopyBufferToImage(cb,
                         src->buffer,
                         ext->dst,
                         ext->dst_layout,
                         ext->region_count,
                         ext->regions);
}

//
//
//

static spn_result_t
spn_ri_submit_buffer(struct spn_device * const device, spn_render_submit_t const * const submit)
{
  struct spn_render_submit_ext_vk_buffer const * const ext = submit->ext;

  //
  // acquire a dispatch
  //
  spn_dispatch_id_t id;

  spn(device_dispatch_acquire(device, SPN_DISPATCH_STAGE_RENDER, &id));

  //
  // declare that the styling and composition happen before this render
  //
  spn_composition_happens_before(submit->composition, id);

  spn_styling_happens_before(submit->styling, id);

  //
  // get a cb
  //
  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, id);

  //
  // clear the surface with white
  //
  if (ext->clear)
    {
      vkCmdFillBuffer(cb, ext->surface.buffer, ext->surface.offset, ext->surface.range, 0xFFFFFFFF);
    }

  //
  // DS: BLOCK POOL
  //
  struct spn_vk * const instance = device->instance;

  spn_vk_ds_bind_render_block_pool(instance, cb, spn_device_block_pool_get_ds(device));

  //
  // DS: TTCKS
  //
  struct spn_vk_ds_ttcks_t ds_ttcks;

  spn_composition_pre_render_bind_ds(submit->composition, &ds_ttcks, cb);

  //
  // DS: STYLING
  //
  struct spn_vk_ds_styling_t ds_styling;

  spn_styling_pre_render_bind_ds(submit->styling, &ds_styling, cb);

  //
  // DS: SURFACE
  //
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
  struct spn_vk_push_render const push = {

    .tile_clip = {
      submit->tile_clip[0],
      submit->tile_clip[1],
      submit->tile_clip[2],
      submit->tile_clip[3],
    },
    .surface_pitch = ext->surface_pitch
  };

  spn_vk_p_push_render(instance, cb, &push);

  //
  // PIPELINE: RENDER
  //
  spn_vk_p_bind_render(instance, cb);

  //
  // wait for buffer clear to finish writing before render writes
  //
  if (ext->clear)
    {
      vk_barrier_transfer_w_to_compute_w(cb);
    }

  //
  // indirect dispatch the pipeline
  //
  spn_composition_pre_render_dispatch_indirect(submit->composition, cb);

  //
  // DEBUG: optionally copy the buffer -- remove when bootstrapping is complete
  //
  if (ext->ext != NULL)
    {
      struct spn_render_submit_ext_base const * const ext_base = ext->ext;

      switch (ext_base->type)
        {
          case SPN_RENDER_SUBMIT_EXT_TYPE_VK_COPY_BUFFER_TO_BUFFER:
            spn_ri_debug_buffer_copy_buffer_to_buffer(ext->ext, &ext->surface, cb);
            break;

          case SPN_RENDER_SUBMIT_EXT_TYPE_VK_COPY_BUFFER_TO_IMAGE:
            spn_ri_debug_buffer_copy_buffer_to_image(ext->ext, &ext->surface, cb);
            break;

          default:
            break;
        }
    }

  //
  // set the completion payload
  //
  struct spn_ri_complete_payload * const payload =
    spn_device_dispatch_set_completion(device, id, spn_ri_complete, sizeof(*payload));

  payload->composition = submit->composition;
  payload->styling     = submit->styling;
  payload->device      = device;
  payload->ds.ttcks    = ds_ttcks;
  payload->ds.styling  = ds_styling;
  payload->ds.surface  = ds_surface;

  //
  // submit the dispatch
  //
  spn_device_dispatch_submit(device, id);

  //
  // FIXME(allanmac): the submit info dependencies will be narrowed
  //
#if 0
  //
  // boilerplate submit
  //
  struct VkSubmitInfo si;

  struct VkSubmitInfo const * const ext_si = ext->si;

  if (ext_si == NULL)
    {
      si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      si.pNext                = NULL;
      si.waitSemaphoreCount   = 0;
      si.pWaitSemaphores      = NULL;
      si.pWaitDstStageMask    = NULL;
      si.commandBufferCount   = 1;
      si.pCommandBuffers      = &cb;
      si.signalSemaphoreCount = 0;
      si.pSignalSemaphores    = NULL;
    }
  else
    {
      //
      // FIXME(allanmac): it's probably reasonable to assert some limit
      // on the max number of command buffers we're willing to accept
      // here... especially if VLA's are not allowed.
      //
      uint32_t const cb_count = ext_si->commandBufferCount + 1;

      VkCommandBuffer cbs[cb_count];

      memcpy(cbs, ext_si->pCommandBuffers, ext_si->commandBufferCount * sizeof(*cbs));

      cbs[ext_si->commandBufferCount] = cb;

      si.sType                = ext_si->sType;
      si.pNext                = ext_si->pNext;
      si.waitSemaphoreCount   = ext_si->waitSemaphoreCount;
      si.pWaitSemaphores      = ext_si->pWaitSemaphores;
      si.pWaitDstStageMask    = ext_si->pWaitDstStageMask;
      si.commandBufferCount   = cb_count;
      si.pCommandBuffers      = cbs;
      si.signalSemaphoreCount = ext_si->signalSemaphoreCount;
      si.pSignalSemaphores    = ext_si->pSignalSemaphores;
    }
#endif

  return SPN_SUCCESS;
}

//
//
//

spn_result_t
spn_render_impl(struct spn_device * const device, spn_render_submit_t const * const submit)
{
  //
  // seal the composition
  //
  {
    spn_result_t const res_c = spn_composition_seal(submit->composition);

    if (res_c)
      {
        return res_c;
      }
  }

  //
  // seal the styling
  //
  {
    spn_result_t const res_s = spn_styling_seal(submit->styling);

    if (res_s)
      {
        return res_s;
      }
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
        return spn_ri_submit_buffer(device, submit);

      case SPN_RENDER_SUBMIT_EXT_TYPE_VK_IMAGE:
      default:
        return SPN_ERROR_NOT_IMPLEMENTED;
    }
}

//
//
//
