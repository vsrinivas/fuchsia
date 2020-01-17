// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_vk_submit_state.h"

#include "spinel/spinel_vk.h"
#include "tests/common/vk_utils.h"  // For vk() macro and vk_submit_one().

// This callback will be invoked by Spinel to submit a command buffer to the
// compute queue. Use it to wait on the swapchain image acquisition semaphore,
// and signal the image rendered semaphore.
static void
spinel_vk_submit_state_callback(VkQueue queue, VkFence fence, VkCommandBuffer const cb, void * data)
{
  SpinelVkSubmitState * state = data;

  vk_submit_one(state->wait_semaphore,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                state->signal_semaphore,
                queue,
                cb,
                fence);

  state->submit_not_enqueued = false;

  if (state->post_callback)
    (*state->post_callback)(state->post_opaque);
}

// Append an extension to the chain.
static void
spinel_vk_submit_state_add_chain(SpinelVkSubmitState * state, void * ext)
{
  *state->chain_tail_ptr = ext;
  state->chain_tail_ptr  = (void **)ext;
  *state->chain_tail_ptr = NULL;
}

void
spinel_vk_submit_state_reset(SpinelVkSubmitState * state,
                             VkImage               image,
                             VkImageView           image_view,
                             VkSampler             image_sampler,
                             VkSemaphore           wait_semaphore,
                             VkSemaphore           signal_semaphore)
{
  state->wait_semaphore      = wait_semaphore;
  state->signal_semaphore    = signal_semaphore;
  state->submit_not_enqueued = true;
  state->post_callback       = NULL;
  state->post_opaque         = NULL;

  state->chain_head     = NULL;
  state->chain_tail_ptr = &state->chain_head;

  state->render = (const spn_vk_render_submit_ext_image_render_t){
    .type  = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_RENDER,
    .image = image,
    .image_info =
      (const VkDescriptorImageInfo){
        .sampler     = image_sampler,
        .imageView   = image_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      },
    .submitter_pfn  = spinel_vk_submit_state_callback,
    .submitter_data = state,
  };

  spinel_vk_submit_state_add_chain(state, &state->render);
}

void
spinel_vk_submit_state_set_post_callback(SpinelVkSubmitState *             state,
                                         SpinelVkSubmitStatePostCallback * post_callback,
                                         void *                            post_opaque)
{
  state->post_callback = post_callback;
  state->post_opaque   = post_opaque;
}

void
spinel_vk_submit_state_add_clear(SpinelVkSubmitState * state, const VkClearColorValue clear_value)
{
  state->pre_clear = (const spn_vk_render_submit_ext_image_pre_clear_t){
    .type  = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_CLEAR,
    .color = &state->clear_color,
  };
  state->clear_color = clear_value;
  spinel_vk_submit_state_add_chain(state, &state->pre_clear);
}

void
spinel_vk_submit_state_add_pre_layout_transition(SpinelVkSubmitState * state,
                                                 VkImageLayout         old_layout)
{
  state->pre_barrier = (const spn_vk_render_submit_ext_image_pre_barrier_t){
    .type       = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_BARRIER,
    .old_layout = old_layout,
    .src_qfi    = VK_QUEUE_FAMILY_IGNORED,
  };
  spinel_vk_submit_state_add_chain(state, &state->pre_barrier);
}

void
spinel_vk_submit_state_add_post_layout_transition(SpinelVkSubmitState * state,
                                                  VkImageLayout         new_layout)
{
  state->post_barrier = (const spn_vk_render_submit_ext_image_post_barrier_t){
    .type       = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_POST_BARRIER,
    .new_layout = new_layout,
    .dst_qfi    = VK_QUEUE_FAMILY_IGNORED,
  };
  spinel_vk_submit_state_add_chain(state, &state->post_barrier);
}

void
spinel_vk_submit_state_add_post_copy_to_buffer(SpinelVkSubmitState * state,
                                               VkBuffer              buffer,
                                               VkExtent2D            extent)
{
  state->post_copy_to_buffer = (const spn_vk_render_submit_ext_image_post_copy_to_buffer_t){
    .type         = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_POST_COPY_TO_BUFFER,
    .dst          = buffer,
    .region_count = 1,
    .regions      = &state->buffer_image_copy,
  };

  state->buffer_image_copy = (const VkBufferImageCopy){
      .bufferOffset      = 0,
      .bufferRowLength   = extent.width,
      .bufferImageHeight = extent.height,
      .imageSubresource = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel       = 0,
        .baseArrayLayer = 0,
        .layerCount     = 1,
      },
      .imageOffset = {
        .x = 0,
        .y = 0,
        .z = 0,
      },
      .imageExtent = {
        .width  = extent.width,
        .height = extent.height,
        .depth  = 1,
      },
  };
  spinel_vk_submit_state_add_chain(state, &state->post_copy_to_buffer);
}

void *
spinel_vk_submit_state_get_ext(const SpinelVkSubmitState * state)
{
  return state->chain_head;
}

bool
spinel_vk_submit_state_was_submitted(const SpinelVkSubmitState * state)
{
  return !state->submit_not_enqueued;
}
