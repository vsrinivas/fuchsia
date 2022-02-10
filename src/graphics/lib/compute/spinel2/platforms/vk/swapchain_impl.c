// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "swapchain_impl.h"

#include "common/util.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "composition_impl.h"
#include "spinel/platforms/vk/spinel_vk_types.h"
#include "styling_impl.h"

//
//
//

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// VK
//
struct spinel_sc_vk
{
  struct spinel_dbi_dm dbi_dm;

  struct
  {
    spinel_extent_2d_t size;
    uint32_t           count;
    uint32_t           range;
  } extent;

  struct
  {
    VkPipelineStageFlags * stages;      // initialized to TOP_OF_PIPE
    VkSemaphore *          semaphores;  //
    uint64_t *             values;      // initialized to zero
  } timeline;
};

//
// IMPL
//
struct spinel_swapchain_impl
{
  struct spinel_swapchain * swapchain;
  struct spinel_device *    device;

  //
  // Vulkan resources
  //
  struct spinel_sc_vk vk;
};

//
// Hold pointers to all the extensions
//
struct spinel_sc_exts
{
  spinel_swapchain_submit_t const * submit;

  union
  {
    struct
    {
      struct
      {
        spinel_vk_swapchain_submit_ext_compute_wait_t const *    wait;
        spinel_vk_swapchain_submit_ext_compute_acquire_t const * acquire;
        spinel_vk_swapchain_submit_ext_compute_fill_t const *    fill;
        spinel_vk_swapchain_submit_ext_compute_render_t const *  render;
        spinel_vk_swapchain_submit_ext_compute_copy_t const *    copy;
        spinel_vk_swapchain_submit_ext_compute_release_t const * release;
        spinel_vk_swapchain_submit_ext_compute_signal_t const *  signal;
      } compute;
      struct
      {
        spinel_vk_swapchain_submit_ext_graphics_wait_t const *   wait;
        spinel_vk_swapchain_submit_ext_graphics_clear_t const *  clear;
        spinel_vk_swapchain_submit_ext_graphics_store_t const *  store;
        spinel_vk_swapchain_submit_ext_graphics_signal_t const * signal;
      } graphics;
    } named;

    spinel_vk_swapchain_submit_ext_base_t const * base[SPN_VK_SWAPCHAIN_SUBMIT_EXT_COUNT];
  };
};

//
// Scan all the extensions
//
// The `exts` arguments must be zeroed.
//
static void
spinel_sc_exts_scan(spinel_swapchain_submit_t const * submit, struct spinel_sc_exts * exts)
{
  //
  // Save render submit
  //
  exts->submit = submit;

  //
  // Accumulate extensions
  //
  if (submit != NULL)
    {
      void * ext_next = submit->ext;

      while (ext_next != NULL)
        {
          spinel_vk_swapchain_submit_ext_base_t * const base = ext_next;

          // Simply ignore if out of range
          if (base->type < SPN_VK_SWAPCHAIN_SUBMIT_EXT_COUNT)
            {
              exts->base[base->type] = ext_next;
            }

          ext_next = base->ext;
        }
    }
}

//
// Validate submission and extensions
//
// For now, only perform cursory validation
//
static bool
spinel_sc_exts_validate(struct spinel_swapchain_impl * impl, struct spinel_sc_exts * exts)
{
  if (exts->submit == NULL)
    {
      return false;
    }
  else if (exts->named.compute.render == NULL)
    {
      return false;
    }
  else if (exts->named.compute.render->extent_index >= impl->vk.extent.count)
    {
      return false;
    }
  else
    {
      return true;
    }
}

//
// BUFFER EXTENSIONS
//
// NOTE(allanmac): The extensions are always processed in the enum order.
//
static VkPipelineStageFlags
spinel_sc_render_record(VkCommandBuffer cb, void * data0, void * data1)
{
  struct spinel_swapchain_impl * const impl   = data0;
  struct spinel_sc_exts const * const  exts   = data1;
  struct spinel_device * const         device = impl->device;

  //
  // Which extent?
  //
  VkDescriptorBufferInfo const dbi = {
    .buffer = impl->vk.dbi_dm.dbi.buffer,
    .offset = impl->vk.extent.range * exts->named.compute.render->extent_index,
    .range  = impl->vk.extent.range,
  };

  //
  // Starting stage/access
  //
  VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkAccessFlags        src_mask  = VK_ACCESS_NONE_KHR;

  //
  // Are swapchain resources exclusive or concurrent?
  //
  bool const is_exclusive = (device->ti.config.swapchain.sharing_mode == VK_SHARING_MODE_EXCLUSIVE);

  //
  // SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_ACQUIRE
  //
  // FIXME(allanmac): It's more elegant to have each stage initiate the prior
  // barrier on demand.  Turn this into a function and push it downward.
  //
  if ((exts->named.compute.acquire != NULL) && is_exclusive)
    {
      // clang-format off
      bool const is_queue_neq = (device->vk.q.compute.create_info.family_index != exts->named.compute.acquire->from_queue_family_index);
      bool const is_qfo_xfer  = (is_exclusive && is_queue_neq);
      // clang-format on

      //
      // Skip the queue family ownership transfer if it's a noop
      //
      if (is_qfo_xfer)
        {
          bool const is_fill = (exts->named.compute.fill != NULL);

          // clang-format off
          VkAccessFlags        xfer_mask  = is_fill ? VK_ACCESS_TRANSFER_WRITE_BIT   : VK_ACCESS_SHADER_WRITE_BIT;
          VkPipelineStageFlags xfer_stage = is_fill ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
          // clang-format on

          VkBufferMemoryBarrier const bmb = {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext               = NULL,
            .srcAccessMask       = VK_ACCESS_NONE_KHR,
            .dstAccessMask       = xfer_mask,
            .srcQueueFamilyIndex = exts->named.compute.acquire->from_queue_family_index,
            .dstQueueFamilyIndex = device->vk.q.compute.create_info.family_index,
            .buffer              = dbi.buffer,
            .offset              = dbi.offset,
            .size                = dbi.range,
          };

          vkCmdPipelineBarrier(cb,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               xfer_stage,
                               0,
                               0,
                               NULL,
                               1,
                               &bmb,
                               0,
                               NULL);
        }
    }

  //
  // SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_FILL
  //
  if (exts->named.compute.fill != NULL)
    {
      vkCmdFillBuffer(cb, dbi.buffer, dbi.offset, dbi.range, exts->named.compute.fill->dword);

      //
      // Outgoing stage/access
      //
      src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      src_mask  = VK_ACCESS_TRANSFER_WRITE_BIT;
    }

  //
  // SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_RENDER
  //
  {
    //
    // Push:   push.ttcks
    // Direct: render dispatch pipeline
    //
    // FIXME(allanmac): Is there a better way to discover the src_stage and
    // src_mask versus inspection of this function?
    //
    spinel_composition_push_render_dispatch_record(exts->submit->composition, cb);

    //
    // Outgoing stage/access
    //
    // clang-format off
    src_stage |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    src_mask  |= VK_ACCESS_SHADER_WRITE_BIT;
    // clang-format on

    vk_memory_barrier(cb,
                      src_stage,
                      src_mask,
                      (VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |  //
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT),
                      (VK_ACCESS_INDIRECT_COMMAND_READ_BIT |  //
                       VK_ACCESS_SHADER_READ_BIT));

    //
    // Set up the tile clip
    //
    struct spinel_target_config const * const config = &device->ti.config;

    uint32_t const tile_w = 1 << config->tile.width_log2;
    uint32_t const tile_h = 1 << config->tile.height_log2;

    uint32_t const surf_w = tile_w << SPN_TTCK_HI_BITS_X;
    uint32_t const surf_h = tile_h << SPN_TTCK_HI_BITS_Y;

    uint32_t const clip_x0 = MIN_MACRO(uint32_t, exts->named.compute.render->clip.x0, surf_w);
    uint32_t const clip_y0 = MIN_MACRO(uint32_t, exts->named.compute.render->clip.y0, surf_h);

    uint32_t const tile_w_mask = tile_w - 1;
    uint32_t const tile_h_mask = tile_h - 1;

    // clang-format off
    uint32_t const clip_x1 = MIN_MACRO(uint32_t, exts->named.compute.render->clip.x1, surf_w) + tile_w_mask;
    uint32_t const clip_y1 = MIN_MACRO(uint32_t, exts->named.compute.render->clip.y1, surf_h) + tile_h_mask;

    uint32_t const surf_clip_x0 = MIN_MACRO(uint32_t, clip_x0, impl->vk.extent.size.width);
    uint32_t const surf_clip_y0 = MIN_MACRO(uint32_t, clip_y0, impl->vk.extent.size.height);
    uint32_t const surf_clip_x1 = MIN_MACRO(uint32_t, clip_x1, impl->vk.extent.size.width);
    uint32_t const surf_clip_y1 = MIN_MACRO(uint32_t, clip_y1, impl->vk.extent.size.height);
    // clang-format on

    //
    // Render push constants
    //
    // Note that .tile_clip is an i32vec4
    //
    struct spinel_push_render push_render = {
      .tile_clip = {
        .x = surf_clip_x0 >> config->tile.width_log2,
        .y = surf_clip_y0 >> config->tile.height_log2,
        .z = surf_clip_x1 >> config->tile.width_log2,
        .w = surf_clip_y1 >> config->tile.height_log2,
      },
      .devaddr_block_pool_ids    = device->block_pool.vk.dbi_devaddr.ids.devaddr,
      .devaddr_block_pool_blocks = device->block_pool.vk.dbi_devaddr.blocks.devaddr,
      .devaddr_surface           = spinel_dbi_to_devaddr(device->vk.d,&dbi),
      .row_pitch                 = impl->vk.extent.size.width,
    };

    //
    // Inits: push.styling
    //
    // FIXME(allanmac): Is there a better way to discover the src_stage and
    // src_mask versus inspection of this function?
    //
    spinel_styling_push_render_init(exts->submit->styling, &push_render);

    //
    // Inits:    push.ttcks
    //           push.ttck_keyvals
    // Indirect: render pipeline
    //
    // FIXME(allanmac): Is there a better way to discover the src_stage and
    // src_mask versus inspection of this function?
    //
    spinel_composition_push_render_init_record(exts->submit->composition, &push_render, cb);

    //
    // Outgoing stage/access
    //
    src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    src_mask  = VK_ACCESS_SHADER_WRITE_BIT;
  }

  //
  // SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_COPY_TO_BUFFER
  //
  if (exts->named.compute.copy != NULL)
    {
      vk_memory_barrier(cb,
                        src_stage,
                        src_mask,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT);

      // Copy the smaller range
      VkDeviceSize const range = MIN_MACRO(VkDeviceSize,  //
                                           dbi.range,
                                           exts->named.compute.copy->dst.range);

      VkBufferCopy const bcs[] = {
        { .srcOffset = dbi.offset,
          .dstOffset = exts->named.compute.copy->dst.offset,
          .size      = range },
      };

      vkCmdCopyBuffer(cb, dbi.buffer, exts->named.compute.copy->dst.buffer, 1, bcs);

      //
      // Outgoing stage/access
      //
      src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      src_mask  = VK_ACCESS_TRANSFER_WRITE_BIT;
    }

  //
  // SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_RELEASE
  //
  if ((exts->named.compute.release != NULL) && is_exclusive)
    {
      // clang-format off
      bool const is_queue_neq = (device->vk.q.compute.create_info.family_index != exts->named.compute.release->to_queue_family_index);
      bool const is_qfo_xfer  = (is_exclusive && is_queue_neq);
      // clang-format on

      //
      // Skip the queue family ownership transfer if it's a noop
      //
      if (is_qfo_xfer)
        {
          VkBufferMemoryBarrier const bmb = {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext               = NULL,
            .srcAccessMask       = src_mask,
            .dstAccessMask       = VK_ACCESS_NONE_KHR,
            .srcQueueFamilyIndex = device->vk.q.compute.create_info.family_index,
            .dstQueueFamilyIndex = exts->named.compute.release->to_queue_family_index,
            .buffer              = dbi.buffer,
            .offset              = dbi.offset,
            .size                = dbi.range,
          };

          vkCmdPipelineBarrier(cb,
                               src_stage,
                               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                               0,
                               0,
                               NULL,
                               1,
                               &bmb,
                               0,
                               NULL);
        }
    }

  //
  // Return the final stage
  //
  return src_stage;
}

//
// Store rendered buffer to an image
//
// TODO(allanmac): This eventually becomes an indirectly dispatched vertex
// shader followed by a fragment shader.
//
static void
spinel_sc_graphics(struct spinel_swapchain_impl * impl, struct spinel_sc_exts const * exts)
{
  struct spinel_device * const device       = impl->device;
  uint32_t const               extent_index = exts->named.compute.render->extent_index;

  //
  // Begin command buffer
  //
  VkCommandBufferBeginInfo const cbbi = {

    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .pInheritanceInfo = NULL
  };

  VkCommandBuffer cb = exts->named.graphics.store->cb;

  vk(BeginCommandBuffer(cb, &cbbi));

  //
  // Which extent?
  //
  VkDescriptorBufferInfo const dbi = {
    .buffer = impl->vk.dbi_dm.dbi.buffer,
    .offset = impl->vk.extent.range * exts->named.graphics.store->extent_index,
    .range  = impl->vk.extent.range,
  };

  //
  // Is a queue family ownership transfer of the compute extent to the graphics
  // queue required?
  //
  // clang-format off
  bool const is_exclusive = (device->ti.config.swapchain.sharing_mode == VK_SHARING_MODE_EXCLUSIVE);
  bool const is_queue_neq = (device->vk.q.compute.create_info.family_index != exts->named.graphics.store->queue_family_index);
  bool const is_qfo_xfer  = (is_exclusive && is_queue_neq);
  // clang-format on

  //
  // Is a queue family ownership transfer "acquire" required?
  //
  if (is_qfo_xfer)
    {
      VkBufferMemoryBarrier const bmb = {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext               = NULL,
        .srcAccessMask       = VK_ACCESS_NONE_KHR,
        .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = device->vk.q.compute.create_info.family_index,
        .dstQueueFamilyIndex = exts->named.graphics.store->queue_family_index,
        .buffer              = dbi.buffer,
        .offset              = dbi.offset,
        .size                = dbi.range,
      };

      vkCmdPipelineBarrier(cb,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           0,
                           NULL,
                           1,
                           &bmb,
                           0,
                           NULL);
    }

  //
  // Accumulate barrier state
  //
  // Top-of-pipe and zeroes in the member are exactly what we want to start
  // with.
  //
  VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkImageMemoryBarrier imgbar    = {

    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .pNext               = NULL,
    .srcAccessMask       = VK_ACCESS_NONE_KHR,
    .dstAccessMask       = VK_ACCESS_NONE_KHR,
    .oldLayout           = exts->named.graphics.store->old_layout,
    .newLayout           = exts->named.graphics.store->image_info.imageLayout,
    .srcQueueFamilyIndex = exts->named.graphics.store->queue_family_index,
    .dstQueueFamilyIndex = exts->named.graphics.store->queue_family_index,
    .image               = exts->named.graphics.store->image,
    .subresourceRange    = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1 },
  };

  //
  // GRAPHICS CLEAR
  //
  if (exts->named.graphics.clear != NULL)
    {
      imgbar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imgbar.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

      vkCmdPipelineBarrier(cb,
                           src_stage,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           0,
                           NULL,
                           0,
                           NULL,
                           1,
                           &imgbar);

      vkCmdClearColorImage(cb,
                           exts->named.graphics.store->image,
                           imgbar.newLayout,
                           &exts->named.graphics.clear->color,
                           1,
                           &imgbar.subresourceRange);

      src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

      imgbar.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      imgbar.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

  //
  // GRAPHICS STORE
  //
  {
    imgbar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imgbar.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    vkCmdPipelineBarrier(cb,
                         src_stage,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         NULL,
                         0,
                         NULL,
                         1,
                         &imgbar);

    VkBufferImageCopy const bic = {
      .bufferOffset      = dbi.offset,
      .bufferRowLength   = impl->vk.extent.size.width,
      .bufferImageHeight = impl->vk.extent.size.height,
      .imageSubresource  = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel       = 0,
                             .baseArrayLayer = 0,
                             .layerCount     = 1, },
      .imageOffset       = { 0, 0, 0 },
      .imageExtent       = { .width  = impl->vk.extent.size.width,
                             .height = impl->vk.extent.size.height,
                             .depth  = 1, },
    };

    vkCmdCopyBufferToImage(cb,
                           dbi.buffer,
                           exts->named.graphics.store->image,
                           imgbar.newLayout,
                           1,
                           &bic);

    src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    imgbar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imgbar.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  }

  //
  // Final layout transition
  //
  {
    imgbar.dstAccessMask = VK_ACCESS_NONE_KHR;
    imgbar.newLayout     = exts->named.graphics.store->image_info.imageLayout;

    vkCmdPipelineBarrier(cb,
                         src_stage,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0,
                         0,
                         NULL,
                         0,
                         NULL,
                         1,
                         &imgbar);
  }

  //
  // Is a queue family ownership transfer "release" required?
  //
  if (is_qfo_xfer)
    {
      VkBufferMemoryBarrier const bmb = {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext               = NULL,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_NONE_KHR,
        .srcQueueFamilyIndex = exts->named.graphics.store->queue_family_index,
        .dstQueueFamilyIndex = device->vk.q.compute.create_info.family_index,
        .buffer              = dbi.buffer,
        .offset              = dbi.offset,
        .size                = dbi.range,
      };

      vkCmdPipelineBarrier(cb,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           0,
                           0,
                           NULL,
                           1,
                           &bmb,
                           0,
                           NULL);
    }
  //
  // End command buffer
  //
  vk(EndCommandBuffer(cb));

  //
  // There is a bug with Mesa 21.x when ANV_QUEUE_THREAD_DISABLE is defined.
  //
  // See: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=92433
  //
  // FIXME(allanmac): This workaround exacts some performance. Remove it as soon
  // as it's feasible.
  //
  if (device->vk.workaround.mesa_21_anv)
    {
      VkSemaphoreWaitInfo const swi = {
        .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .pNext          = NULL,
        .flags          = 0,  // flag doesn't matter when there is 1 semaphore
        .semaphoreCount = 1,
        .pSemaphores    = impl->vk.timeline.semaphores + extent_index,
        .pValues        = impl->vk.timeline.values + extent_index,
      };

      //
      // Wait for semaphore to complete...
      //
      if (vkWaitSemaphores(device->vk.d, &swi, UINT64_MAX) != VK_SUCCESS)
        {
          exit(EXIT_FAILURE);
        }
    }

  //
  // Submit the command buffer with its associated wait and signal semaphores.
  //
  // Note that the graphics submission waits upon the completion of the compute
  // submission and any future use of the associated storage buffer extent must
  // wait upon the completion of the graphics submission.
  //
  // clang-format off
  uint32_t const       wait_count                                               = 1 + exts->named.graphics.wait->wait.count;
  VkPipelineStageFlags wait_stages      [SPN_VK_SEMAPHORE_IMPORT_WAIT_SIZE + 1] = { impl->vk.timeline.stages[extent_index]     };
  VkSemaphore          wait_semaphores  [SPN_VK_SEMAPHORE_IMPORT_WAIT_SIZE + 1] = { impl->vk.timeline.semaphores[extent_index] };
  uint64_t             wait_values      [SPN_VK_SEMAPHORE_IMPORT_WAIT_SIZE + 1] = { impl->vk.timeline.values[extent_index]++   }; // increment!

  uint32_t const       signal_count                                               = 1 + exts->named.graphics.signal->signal.count;
  VkSemaphore          signal_semaphores[SPN_VK_SEMAPHORE_IMPORT_SIGNAL_SIZE + 1] = { impl->vk.timeline.semaphores[extent_index] };
  uint64_t             signal_values    [SPN_VK_SEMAPHORE_IMPORT_SIGNAL_SIZE + 1] = { impl->vk.timeline.values[extent_index]     };
  // clang-format on

  VkTimelineSemaphoreSubmitInfo const tssi = {
    .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
    .pNext                     = NULL,
    .waitSemaphoreValueCount   = wait_count,
    .pWaitSemaphoreValues      = wait_values,
    .signalSemaphoreValueCount = signal_count,
    .pSignalSemaphoreValues    = signal_values,
  };

  VkSubmitInfo const submit_info = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = &tssi,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cb,
    .waitSemaphoreCount   = wait_count,
    .pWaitSemaphores      = wait_semaphores,
    .pWaitDstStageMask    = wait_stages,
    .signalSemaphoreCount = signal_count,
    .pSignalSemaphores    = signal_semaphores,
  };

  //
  // GRAPHICS WAIT
  //
  if (exts->named.graphics.wait != NULL)
    {
      memcpy(wait_stages + 1,
             exts->named.graphics.wait->wait.stages,
             sizeof(wait_stages[0]) * exts->named.graphics.wait->wait.count);

      memcpy(wait_semaphores + 1,
             exts->named.graphics.wait->wait.semaphores,
             sizeof(wait_semaphores[0]) * exts->named.graphics.wait->wait.count);

      memcpy(wait_values + 1,
             exts->named.graphics.wait->wait.values,
             sizeof(wait_values[0]) * exts->named.graphics.wait->wait.count);
    }

  //
  // GRAPHICS SIGNAL
  //
  if (exts->named.graphics.signal != NULL)
    {
      memcpy(signal_semaphores + 1,
             exts->named.graphics.signal->signal.semaphores,
             sizeof(signal_semaphores[0]) * exts->named.graphics.signal->signal.count);

      memcpy(signal_values + 1,
             exts->named.graphics.signal->signal.values,
             sizeof(signal_values[0]) * exts->named.graphics.signal->signal.count);
    }

  //
  // Submit
  //
  vk(QueueSubmit(exts->named.graphics.store->queue, 1, &submit_info, VK_NULL_HANDLE));
}

//
//
//
static void
spinel_sc_render_complete(void * data0, void * data1)
{
  struct spinel_styling * const     styling     = data0;
  struct spinel_composition * const composition = data1;

  //
  // release locks on styling and composition
  //
  spinel_styling_unlock_and_release(styling);
  spinel_composition_unlock_and_release(composition);
}

//
//
//
static spinel_result_t
spinel_sc_submit(struct spinel_swapchain_impl * impl, spinel_swapchain_submit_t const * submit)
{
  //
  // gather submission and extensions
  //
  struct spinel_sc_exts exts = { 0 };

  spinel_sc_exts_scan(submit, &exts);

  //
  // validate submission
  //
  if (!spinel_sc_exts_validate(impl, &exts))
    {
      return SPN_ERROR_SWAPCHAIN_SUBMIT_INVALID;
    }

  //
  // seal the composition
  //
  {
    spinel_result_t const result = spinel_composition_seal(submit->composition);

    if (result)
      {
        return result;
      }
  }

  //
  // seal the styling
  //
  {
    spinel_result_t const result = spinel_styling_seal(submit->styling);

    if (result)
      {
        return result;
      }
  }

  //
  // acquire an immediate semaphore
  //
  // clang-format off
  spinel_deps_immediate_semaphore_t const dis_s = spinel_styling_retain_and_lock(submit->styling);
  spinel_deps_immediate_semaphore_t const dis_c = spinel_composition_retain_and_lock(submit->composition);
  // clang-format on

  //
  // render
  //
  struct spinel_deps_immediate_submit_info disi = {
    .record = {
      .pfn   = spinel_sc_render_record,
      .data0 = impl,
      .data1 = &exts,
    },
    .completion = {
      .pfn   = spinel_sc_render_complete,
      .data0 = submit->styling,
      .data1 = submit->composition,
    },
  };

  //
  // which extent?
  //
  uint32_t const extent_index = exts.named.compute.render->extent_index;

  //
  // Explicitly set the transfer timeline semaphores
  //
  // TODO: combine .transfer with .import and just size it to handle all use
  // cases.
  //

  // Wait
  disi.wait.transfer.count         = 1,
  disi.wait.transfer.stages[0]     = impl->vk.timeline.stages[extent_index];
  disi.wait.transfer.semaphores[0] = impl->vk.timeline.semaphores[extent_index];
  disi.wait.transfer.values[0]     = impl->vk.timeline.values[extent_index]++;  // increment

  // Signal
  disi.signal.transfer.count         = 1,
  disi.signal.transfer.semaphores[0] = impl->vk.timeline.semaphores[extent_index];
  disi.signal.transfer.values[0]     = impl->vk.timeline.values[extent_index];

  //
  // These depend on whether the styling and composition are sealing or
  // sealed:
  //
  // .wait = {
  //   .immediate = {
  //     .count      = 0/1/2,
  //     .semaphores = { dis_s, dis_c },
  //   },
  //   .import = { ... }
  // },
  // .signal = {
  //   import = { ... }
  // },
  //
  if (dis_s != SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID)
    {
      disi.wait.immediate.semaphores[disi.wait.immediate.count++] = dis_s;
    }

  if (dis_c != SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID)
    {
      disi.wait.immediate.semaphores[disi.wait.immediate.count++] = dis_c;
    }

  if (exts.named.compute.wait != NULL)
    {
      disi.wait.import = exts.named.compute.wait->wait;
    }

  if (exts.named.compute.signal != NULL)
    {
      disi.signal.import = exts.named.compute.signal->signal;
    }

  //
  // Compute extensions are submitted on a compute queue
  //
  struct spinel_device * const device = impl->device;

  spinel_deps_immediate_semaphore_t immediate;

  spinel_deps_immediate_submit(device->deps, &device->vk, &disi, &immediate);

  //
  // Save wait mask
  //
  impl->vk.timeline.stages[extent_index] = spinel_deps_immediate_get_stage(device->deps, immediate);

  //
  // Submit graphics extensions on the provided queue
  //
  if (exts.named.graphics.store != NULL)
    {
      spinel_sc_graphics(impl, &exts);
    }

  return SPN_SUCCESS;
}

//
////
static spinel_result_t
spinel_sc_release(struct spinel_swapchain_impl * impl)
{
  //
  //
  //
  struct spinel_device * const device = impl->device;

  //
  // Wait for timeline semaphores.
  //
  // For now, just block until all outstanding renders are complete.
  //
  // Note that it's not strong enough of a guarantee to wait upon the the
  // swapchain's timeline semaphores as the extent maybe still be in use by a
  // compute-to-graphics copy.
  //
  // TODO(allanmac): It may be useful to release these resources asynchronously
  // using the `deps` logic (with some modifications).  This might reduce
  // latency of disposal and reallocation of a swapchain during window resize.
  //
#if 0
  VkSemaphoreWaitInfo const swi = {
    .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    .pNext          = NULL,
    .flags          = 0,  // defaults to "wait_all"
    .semaphoreCount = impl->vk.extent.count,
    .pSemaphores    = impl->vk.timeline.semaphores,
    .pValues        = impl->vk.timeline.values,
  };

  vk(WaitSemaphores(impl->device->vk.d, &swi, UINT64_MAX));
#else
  vk(DeviceWaitIdle(device->vk.d));
#endif

  //
  // Free swapchain
  //

  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw_shared,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.dbi_dm);

  //
  // Destroy semaphores
  //
  for (uint32_t ii = 0; ii < impl->vk.extent.count; ii++)
    {
      vkDestroySemaphore(device->vk.d, impl->vk.timeline.semaphores[ii], device->vk.ac);
    }

  //
  // Free host allocations
  //
  free(impl->vk.timeline.values);
  free(impl->vk.timeline.semaphores);
  free(impl->vk.timeline.stages);

  free(impl->swapchain);
  free(impl);

  spinel_context_release(device->context);

  return SPN_SUCCESS;
}

//
//
//
spinel_result_t
spinel_swapchain_impl_create(struct spinel_device *                 device,
                             spinel_swapchain_create_info_t const * create_info,
                             spinel_swapchain_t *                   swapchain)
{
  spinel_context_retain(device->context);

  //
  // allocate impl
  //
  struct spinel_swapchain_impl * const impl = malloc(sizeof(*impl));

  //
  // allocate swapchain
  //
  struct spinel_swapchain * const s = *swapchain = malloc(sizeof(*s));

  //
  // init forward/backward pointers
  //
  impl->swapchain = s;
  impl->device    = device;
  s->impl         = impl;

  //
  // initialize swapchain pfns
  //
  s->release   = spinel_sc_release;
  s->submit    = spinel_sc_submit;
  s->ref_count = 1;

  //
  // Create one timeline semaphore per swapchain extent
  //
  // Note that VK_PIPELINE_STAGE_NONE_KHR equals 0
  //
  impl->vk.timeline.stages     = malloc(create_info->count * sizeof(*impl->vk.timeline.stages));
  impl->vk.timeline.semaphores = malloc(create_info->count * sizeof(*impl->vk.timeline.semaphores));
  impl->vk.timeline.values     = calloc(create_info->count, sizeof(*impl->vk.timeline.values));

  VkSemaphoreTypeCreateInfo const stci = {

    .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
    .pNext         = NULL,
    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    .initialValue  = 0UL
  };

  VkSemaphoreCreateInfo const sci = {

    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = &stci,
    .flags = 0
  };

  //
  // Create timeline semaphores waiting at top of pipe and initialized to 0
  //
  for (uint32_t ii = 0; ii < create_info->count; ii++)
    {
      impl->vk.timeline.stages[ii] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

      vk(CreateSemaphore(device->vk.d, &sci, device->vk.ac, impl->vk.timeline.semaphores + ii));
    }

  //
  // get storage buffer offset alignment -- assumed to be pow2
  //
  VkPhysicalDeviceProperties vk_pdp;

  vkGetPhysicalDeviceProperties(device->vk.pd, &vk_pdp);

  uint32_t const offset_alignment = (uint32_t)vk_pdp.limits.minStorageBufferOffsetAlignment;

  //
  // get tile alignment -- assumed to be pow2
  //
  struct spinel_target_config const * config = &device->ti.config;

  uint32_t const tile_alignment = 1U << (config->tile.width_log2 + config->tile.height_log2);

  //
  // get max alignment -- assumed to be pow2
  //
  uint32_t const alignment = MAX_MACRO(uint32_t, offset_alignment, tile_alignment);

  assert(is_pow2_u32(alignment));

  //
  // initialize swapchain extent
  //
  uint32_t const extent_size = create_info->extent.width *   //
                               create_info->extent.height *  //
                               config->swapchain.texel_size;

  uint32_t const extent_size_ru = ROUND_UP_POW2_MACRO(extent_size, alignment);

  impl->vk.extent.size  = create_info->extent;
  impl->vk.extent.count = create_info->count;
  impl->vk.extent.range = extent_size_ru;

  //
  // Allocate swapchain extents
  //
  // Rendering to the storage buffer occurs on a compute queue.
  //
  // The Spinel target's config determines whether the `drw_shared` allocator
  // allocates the storage buffer with either VK_SHARING_MODE_EXCLUSIVE or
  // VK_SHARING_MODE_CONCURRENT.
  //
  // Optional rendering to an image occurs on a graphics queue.
  //
  VkDeviceSize const swapchain_size = extent_size_ru * create_info->count;

  spinel_allocator_alloc_dbi_dm(&device->allocator.device.perm.drw_shared,
                                device->vk.pd,
                                device->vk.d,
                                device->vk.ac,
                                swapchain_size,
                                NULL,
                                &impl->vk.dbi_dm);

  return SPN_SUCCESS;
}

//
//
//
