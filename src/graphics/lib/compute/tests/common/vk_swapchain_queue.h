// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SWAPCHAIN_QUEUE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SWAPCHAIN_QUEUE_H_

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vk_swapchain.h"

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

// |vk_swapchain_queue_t| implements a convenience struct to simplify simple
// applications that need to render frames to swapchain images. Usage is the
// following:
//
//   1) Call vk_swapchain_queue_create() to create your queue.
//
//      Note that this creates an array of |vk_swapchain_queue_image_t|
//      structs. Each one provides a command_buffer, synchronization fence,
//      image framebuffer and synchronization semaphore.
//
//   2) For each frame to draw:
//
//       a) Call vk_swapchain_queue_acquire_next_image() to acquire the next
//          swapchain image, and ensure its matching command buffer has
//          completed its previous submit. The function returns a pointer to
//          the current |vk_swapchain_queue_image_t| to use in steps below.
//
//       b) Optionally change the content of the framebuffer with
//          vkBeginCmdBuffer ... vkEndCmdBuffer. Otherwise, it's existing
//          content will be used as is.
//
//       c) Call vk_swapchain_queue_submit_and_present_image() to submit
//          the image's command buffer then send the image content's for
//          presentation.
//
//  Example:
//
//      vk_swapchain_queue_t * queue = vk_swapchain_queue_create(...);
//
//      // For each frame
//      {
//        const vk_swapchain_queue_image_t * image =
//          vk_swapchain_queue_acquire_next_image(&queue);
//
//        if (!image) {
//          // display surface was resized or invalidated, exit loop
//          break;
//        }
//
//        // Optionally change the content of |image->command_buffer| here.
//        // If not, the buffer's current content will be used.
//
//        vk_swapchain_queue_submit_and_present_image(&queue);
//      }
//
//      vk_swapchain_queue_destroy(queue);

//
//
//

typedef struct vk_swapchain_queue vk_swapchain_queue_t;

// Configuration parameters to initialize a vk_swapchain_queue_t instance.
//
// |swapchain| is the target |vk_swapchain_t| instance to use by the queue.
//
// |queue_family| and |queue_index| are Vulkan queue family and indices where
// the queue's image-specific command buffers will be allocated/submitted.
//
// |device| and |allocator| are the Vulkan device and allocator function pointers.
//
// |enable_framebuffers| is either null, or a VkRenderPass handle that will be
// used to create image-specific framebuffers.
//
// |enable_sync_semaphores| is the number of per-image sync_semaphores to create.
// These are only useful when performing manual vkQueueSubmit() calls between
// vk_swapchain_queue_acquire_next_image() and vk_swapchain_queue_submit_xxx().
// The value should be <= MAX_VK_SYNC_SEMAPHORES.
//
typedef struct
{
  vk_swapchain_t *              swapchain;
  uint32_t                      queue_family;
  uint32_t                      queue_index;
  VkDevice                      device;
  const VkAllocationCallbacks * allocator;

  VkRenderPass enable_framebuffers;
  uint32_t     sync_semaphores_count;

} vk_swapchain_queue_config_t;

#define MAX_VK_SYNC_SEMAPHORES 4

// The data associated with each swapchain image in a queue.
//
// |image| and |image_view| come directly from the original vk_swapchain_t
// instance.
//
// |command_buffer| is the image-specific command buffer. The caller should
// setup its content at least once before calling
// vk_swapchain_queue_submit_and_present_image().
//
// |fence| is the synchronization fence used when submitting the command buffer
// through vk_swapchain_queue_submit_and_present_image(). Most clients should
// just ignore it.
//
// |framebuffer| is an optional VkFramebuffer instance associated with |image|.
// It is VK_NULL_HANDLE by default, unless the queue is created with a non-null
// |vk_swapchain_queue_config_t::enable_framebuffers| value.
//
// |sync_semaphores| is an array of additional semaphores available to the client.
// This can be useful when performing manual submits between
// vk_swapchain_queue_acquire_next_image() and
// vk_swapchain_queue_submit_and_present_image().
// All entriesa re VK_NULL_HANDLE by default, unless
// |vk_swapchain_queue_config_t::sync_semaphores_count| is > 0.
//
typedef struct
{
  VkImage         image;
  VkImageView     image_view;
  VkCommandBuffer command_buffer;
  VkFramebuffer   framebuffer;
  VkFence         fence;
  VkSemaphore     sync_semaphores[MAX_VK_SYNC_SEMAPHORES];
} vk_swapchain_queue_image_t;

typedef struct vk_swapchain_queue vk_swapchain_queue_t;

// Initialize a new swapchain queue instance based on |config|.
extern vk_swapchain_queue_t *
vk_swapchain_queue_create(const vk_swapchain_queue_config_t * config);

extern uint32_t
vk_swapchain_queue_get_size(const vk_swapchain_queue_t * queue);

extern uint32_t
vk_swapchain_queue_get_index(const vk_swapchain_queue_t * queue);

extern const vk_swapchain_queue_image_t *
vk_swapchain_queue_get_image(const vk_swapchain_queue_t * queue, uint32_t image_index);

// Acquire the next swapchain image and ensure its matching command buffer
// has completed its previous submit. On success, return a pointer to the
// current queue image, which is equivalent to |&queue->images[queue->index]|.
// On failure, i.e. if the display surface was invalidated, return NULL.
extern const vk_swapchain_queue_image_t *
vk_swapchain_queue_acquire_next_image(vk_swapchain_queue_t * queue);

// Submit the current image's command buffer, then send the image content for
// presentation. This automatically synchronizes with the swapchain.
extern void
vk_swapchain_queue_submit_and_present_image(vk_swapchain_queue_t * queue);

// A variant of vk_swapchain_queue_submit_and_present_image() which allows the
// submit to wait on a different semaphore. Useful when a manual vkQueueSubmit
// between vk_swapchain_queue_acquire_next_image() and this call.
//
// |wait_semaphore| can be any valid semaphore that has already been enqueued
// for signaling. Clients are encourages to use the |sync_semaphore| of the
// current image.
extern void
vk_swapchain_queue_submit_and_present_image_wait_one(vk_swapchain_queue_t * queue,
                                                     VkSemaphore            wait_semaphore,
                                                     VkPipelineStageFlags   wait_stages);

// Destroy the swapchain queue and release its resources.
extern void
vk_swapchain_queue_destroy(vk_swapchain_queue_t * queue);

//
//
//

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SWAPCHAIN_QUEUE_H_
