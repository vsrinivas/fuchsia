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

struct spn_vk_render_submit_ext_base
{
  void *                          ext;
  spn_vk_render_submit_ext_type_e type;
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
//
//

static spn_result_t
spn_ri_image_render(struct spn_device * const device, spn_render_submit_t const * const submit)
{
  //
  // accumulate extensions
  //
  struct spn_vk_render_submit_ext_image_pre_barrier *         pre_barrier         = NULL;
  struct spn_vk_render_submit_ext_image_pre_clear *           pre_clear           = NULL;
  struct spn_vk_render_submit_ext_image_render *              render              = NULL;
  struct spn_vk_render_submit_ext_image_post_copy_to_buffer * post_copy_to_buffer = NULL;
  struct spn_vk_render_submit_ext_image_post_barrier *        post_barrier        = NULL;

  void * ext_next = submit->ext;

  while (ext_next != NULL)
    {
      struct spn_vk_render_submit_ext_base * const base = ext_next;

      switch (base->type)
        {
          case SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_BARRIER:
            pre_barrier = ext_next;
            break;

          case SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_CLEAR:
            pre_clear = ext_next;
            break;

          case SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_RENDER:
            render = ext_next;
            break;

          case SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_POST_COPY_TO_BUFFER:
            post_copy_to_buffer = ext_next;
            break;

          case SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_POST_BARRIER:
            post_barrier = ext_next;
            break;

          default:
            return SPN_ERROR_RENDER_EXTENSION_INVALID;
        }

      ext_next = base->ext;
    }

  //
  // NOTE(allanmac): The RENDER extension must be in the chain.
  //
  if (render == NULL)
    {
      return SPN_ERROR_RENDER_EXTENSION_INVALID;
    }

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
  // accumulate barrier state
  //
  // NOTE(allanmac): top-of-pipe and zeroes in the member are exactly
  // what we want to start with.
  //
  // NOTE(allanmac): realize that all memory is visible -- image layout
  // transitions and transfers are all we're concerned with.
  //
  VkPipelineStageFlags src_stage    = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkImageMemoryBarrier imgbar       = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
  uint32_t             imgbar_count = 0;

  //
  // set imgbar defaults
  //
  // imgbar.srcAccessMask is 0
  //
  imgbar.oldLayout           = render->image_info.imageLayout;
  imgbar.srcQueueFamilyIndex = device->environment.qfi;
  imgbar.dstQueueFamilyIndex = device->environment.qfi;
  imgbar.image               = render->image;

  //
  // NOTE(allanmac): Simplifying assumption below
  //
  imgbar.subresourceRange = (VkImageSubresourceRange){

    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel   = 0,
    .levelCount     = 1,
    .baseArrayLayer = 0,
    .layerCount     = 1
  };

  //
  // set the submission callback and data
  //
  spn_device_dispatch_set_submitter(device, id, render->submitter_pfn, render->submitter_data);

  //
  // the extensions are always processed in this order:
  //
  //   PRE_BARRIER>PRE_CLEAR>RENDER>POST_COPY>POST_BARRIER
  //

  //
  // layout transition or queue family ownership transfer?
  //
  if (pre_barrier != NULL)
    {
      //
      // imgbar.srcAccessMask       -- use default
      // imgbar.dstAccessMask       -- not set
      // imgbar.dstQueueFamilyIndex -- use default
      // imgbar.image               -- use default
      //
      uint32_t const src_qfi = (pre_barrier->src_qfi == VK_QUEUE_FAMILY_IGNORED)
                                 ? device->environment.qfi
                                 : pre_barrier->src_qfi;

      imgbar.oldLayout           = pre_barrier->old_layout;
      imgbar.newLayout           = render->image_info.imageLayout;
      imgbar.srcQueueFamilyIndex = src_qfi;
      imgbar.image               = render->image;

      imgbar_count = 1;
    }

  //
  // clear?
  //
  if (pre_clear != NULL)
    {
      //
      // imgbar.srcAccessMask       -- use default
      // imgbar.oldLayout           -- use default
      // imgbar.srcQueueFamilyIndex -- use default
      // imgbar.dstQueueFamilyIndex -- use default
      // imgbar.image               -- use default
      //
      imgbar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imgbar.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

      imgbar_count = 1;

      vkCmdPipelineBarrier(cb,
                           src_stage,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           0,
                           NULL,
                           0,
                           NULL,
                           imgbar_count,
                           &imgbar);

      vkCmdClearColorImage(cb,
                           render->image,
                           imgbar.newLayout,
                           pre_clear->color,
                           1,
                           &imgbar.subresourceRange);

      //
      // post command -- transition to render layout
      //
      src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

      //
      // imgbar.dstQueueFamilyIndex -- use default
      // imgbar.image               -- use default
      //
      imgbar.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
      imgbar.oldLayout           = imgbar.newLayout;
      imgbar.newLayout           = render->image_info.imageLayout;
      imgbar.srcQueueFamilyIndex = device->environment.qfi;

      // imgbar_count is 1
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
  *spn_vk_ds_get_surface_surface(instance, ds_surface) = render->image_info;

  // update ds
  spn_vk_ds_update_surface(instance, &device->environment, ds_surface);

  // bind ds
  spn_vk_ds_bind_render_surface(instance, cb, ds_surface);

  //
  // append push constants
  //
  // convert pixel clip coords to tile coords
  //
  // FIXME(allanmac): use the signed SIMD4 trick
  //
  // FIXME(allanmac): this is nearly identical to the composition_impl.c clip
  //
  struct spn_vk_target_config const * const config = spn_vk_get_config(device->instance);

  uint32_t const tile_w = 1 << config->tile.width_log2;
  uint32_t const tile_h = 1 << config->tile.height_log2;

  uint32_t const surf_w_max = tile_w << SPN_TTCK_HI_BITS_X;
  uint32_t const surf_h_max = tile_h << SPN_TTCK_HI_BITS_Y;

  struct spn_uvec4 const tile_clip = {

    submit->clip[0] >> config->tile.width_log2,
    submit->clip[1] >> config->tile.height_log2,

    (MIN_MACRO(uint32_t, submit->clip[2], surf_w_max) + tile_w - 1) >> config->tile.width_log2,
    (MIN_MACRO(uint32_t, submit->clip[3], surf_h_max) + tile_h - 1) >> config->tile.height_log2
  };

  struct spn_vk_push_render const push = {
    .render_clip = { .x = MIN_MACRO(uint32_t, tile_clip.x, 1 << SPN_TTCK_HI_BITS_X),
                     .y = MIN_MACRO(uint32_t, tile_clip.y, 1 << SPN_TTCK_HI_BITS_Y),
                     .z = MIN_MACRO(uint32_t, tile_clip.z, 1 << SPN_TTCK_HI_BITS_X),
                     .w = MIN_MACRO(uint32_t, tile_clip.w, 1 << SPN_TTCK_HI_BITS_Y) },
  };

  spn_vk_p_push_render(instance, cb, &push);

  //
  // PIPELINE: RENDER
  //
  // - indirect dispatch the pipeline
  // - shader only *writes* to surface
  //
  {
    imgbar.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cb,
                         src_stage,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0,
                         NULL,
                         0,
                         NULL,
                         imgbar_count,
                         &imgbar);

    spn_vk_p_bind_render(instance, cb);

    spn_composition_pre_render_dispatch_indirect(submit->composition, cb);

    //
    // post render
    //
    src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    imgbar.srcAccessMask       = imgbar.dstAccessMask;
    imgbar.dstAccessMask       = 0;
    imgbar.oldLayout           = render->image_info.imageLayout;
    imgbar.srcQueueFamilyIndex = device->environment.qfi;
    imgbar.dstQueueFamilyIndex = device->environment.qfi;

    imgbar_count = 0;
  }

  //
  // copy?
  //
  if (post_copy_to_buffer != NULL)
    {
      //
      // imgbar.srcAccessMask       -- use default
      // imgbar.oldLayout           -- use default
      // imgbar.srcQueueFamilyIndex -- use default
      // imgbar.dstQueueFamilyIndex -- use default
      // imgbar.image               -- use default
      //
      imgbar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      imgbar.oldLayout     = render->image_info.imageLayout;
      imgbar.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

      imgbar_count = 1;

      vkCmdPipelineBarrier(cb,
                           src_stage,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           0,
                           NULL,
                           0,
                           NULL,
                           imgbar_count,
                           &imgbar);

      vkCmdCopyImageToBuffer(cb,
                             render->image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             post_copy_to_buffer->dst,
                             post_copy_to_buffer->region_count,
                             post_copy_to_buffer->regions);

      //
      // post copy -- transition the image back to default
      //
      src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

      //
      // imgbar.dstQueueFamilyIndex -- not set
      // imgbar.image               -- use default
      //
      imgbar.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
      imgbar.dstAccessMask       = 0;
      imgbar.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      imgbar.newLayout           = render->image_info.imageLayout;
      imgbar.srcQueueFamilyIndex = device->environment.qfi;  // ignored

      imgbar_count = 1;
    }

  //
  // layout transition or queue family ownership transfer?
  //
  if (post_barrier != NULL)
    {
      //
      // imgbar.srcAccessMask       -- use default
      // imgbar.dstAccessMask       -- use default
      // imgbar.oldLayout           -- use default
      // imgbar.srcQueueFamilyIndex -- use default
      // imgbar.image               -- use default
      //
      uint32_t const dst_qfi = (post_barrier->dst_qfi == VK_QUEUE_FAMILY_IGNORED)
                                 ? device->environment.qfi
                                 : post_barrier->dst_qfi;

      imgbar.newLayout           = post_barrier->new_layout;
      imgbar.dstQueueFamilyIndex = dst_qfi;

      imgbar_count = 1;
    }

  //
  // final barrier
  //
  vkCmdPipelineBarrier(cb,
                       src_stage,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       0,
                       0,
                       NULL,
                       0,
                       NULL,
                       imgbar_count,
                       &imgbar);

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
  struct spn_vk_render_submit_ext_base const * const ext = submit->ext;

  if (ext == NULL)
    {
      return SPN_ERROR_RENDER_EXTENSION_INVALID;
    }

  return spn_ri_image_render(device, submit);
}

//
//
//
