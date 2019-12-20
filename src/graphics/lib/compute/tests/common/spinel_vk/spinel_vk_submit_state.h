// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_VK_SPINEL_VK_SUBMIT_STATE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_VK_SPINEL_VK_SUBMIT_STATE_H_

#include "spinel/spinel_types.h"
#include "spinel/spinel_vk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Convenience data type and functions to manage the chain of Spinel
// Vulkan-specific submit extensions when rendering to Vulkan images.
//
// To render an image with Spinel, do the following:
//
//   - Call spinel_vk_submit_state_reset() first, passing arguments describing
//     the target rendering image an optional wait/signal semaphores that will
//     be used by the Spinel queue submit.
//
//   - Call any of the spinel_vk_submit_state_add_xxx() methods to activate
//     a Vulkan submit extension, if you need them. These functions can be
//     called in any order.
//
//   - Optionally call spinel_vk_submit_state_set_post_callback() to set a
//     a pointer to a callback that will be invoked just after Spinel submits
//     the corresponding command buffer(s) to the compute queue. Might be
//     useful to enqueue other commands as soon as possible.
//
//   - Call spn_render() with an spn_render_submit_t struct whose |ext| field
//     is the result of spinel_vk_submit_state_get_ext().
//
//   - Call spinel_vk_submit_state_wait() to ensure that all related operations
//     have completed. IMPORTANT: The content of a given SpinelVkSubmitState
//     instance should not be modified until all Spinel operations for this
//     image have completed. This is the simplest way to achieve this.
//

// A callback type, invoked after Spinel enqueues its command buffer,
// which may happen inside of spn_render(), or even later.
typedef void(SpinelVkSubmitStatePostCallback)(void * opaque);

typedef struct
{
  // All fields below are private and expected to change in the future.

  // Extensions chain.
  spn_vk_render_submit_ext_image_pre_barrier_t         pre_barrier;
  spn_vk_render_submit_ext_image_pre_clear_t           pre_clear;
  spn_vk_render_submit_ext_image_render_t              render;
  spn_vk_render_submit_ext_image_post_barrier_t        post_barrier;
  spn_vk_render_submit_ext_image_post_copy_to_buffer_t post_copy_to_buffer;
  void *                                               chain_head;
  void **                                              chain_tail_ptr;

  // Other data.
  VkClearColorValue clear_color;
  VkBufferImageCopy buffer_image_copy;
  VkSemaphore       wait_semaphore;
  VkSemaphore       signal_semaphore;
  bool              submit_not_enqueued;

  SpinelVkSubmitStatePostCallback * post_callback;
  void *                            post_opaque;

} SpinelVkSubmitState;

// Reset the |SpinelVkSubmitState| instance and prepare for rendering a new
// image. Call the submit_state_add_xxx() methods after that if you need them,
// then invoke spn_render() with a spn_render_submit_t instance whose |ext|
// set to the result of spinel_vk_submit_state_get_ext().
//
// |image|, |image_view| and |image_sampler| are used by Spinel to access the
// target image.
//
// |wait_semaphore| and |signal_semaphore| are optional semaphores to be used
// by the Spinel queue submit operation.
//
extern void
spinel_vk_submit_state_reset(SpinelVkSubmitState * state,
                             VkImage               image,
                             VkImageView           image_view,
                             VkSampler             image_sampler,
                             VkSemaphore           wait_semaphore,
                             VkSemaphore           signal_semaphore);

// Sets an optional callback that will be called just Spinel enqueues its
// command buffer to the compute queue.
extern void
spinel_vk_submit_state_set_post_callback(SpinelVkSubmitState *             state,
                                         SpinelVkSubmitStatePostCallback * post_callback,
                                         void *                            post_opaque);

// Add an extension to the chain to clear the image.
extern void
spinel_vk_submit_state_add_clear(SpinelVkSubmitState * state, const VkClearColorValue clear_value);

// Add an extension to the chain to perform an image layout transition from
// |old_layout| to whatever Spinel expects to use.
// NOTE: For now assumes all operations happen on the same queue.
extern void
spinel_vk_submit_state_add_pre_layout_transition(SpinelVkSubmitState * state,
                                                 VkImageLayout         old_layout);

// Add an extension to the chain to perform an image layout transition to
// |new_layout| after Spinel has finished rendering.
// NOTE: For now assumes all operations happen on the same queue.
extern void
spinel_vk_submit_state_add_post_layout_transition(SpinelVkSubmitState * state,
                                                  VkImageLayout         new_layout);

// Add an extension to copy the target image to a buffer after Spinel rendering.
// This is typically used to transfer the rendered image to a host-visible buffer
// in order for the CPU to access it.
// Assumes both the buffer and image have the same |extent|.
extern void
spinel_vk_submit_state_add_post_copy_to_buffer(SpinelVkSubmitState * state,
                                               VkBuffer              buffer,
                                               VkExtent2D            extent);

// Retrieve the value of the spn_render_submit_t::ext field to use when calling
// spn_render() for this instance.
extern void *
spinel_vk_submit_state_get_ext(const SpinelVkSubmitState * state);

// After a call to spn_render(), wait until Spinel has properly queued its
// command buffer to the GPU. This is necessary to be able to enqueue a wait on
// the |signal_semaphore| passed to spinel_vk_submit_state_reset().
extern void
spinel_vk_submit_state_wait_enqueued(SpinelVkSubmitState * state, spn_context_t context);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_VK_SPINEL_VK_SUBMIT_STATE_H_
