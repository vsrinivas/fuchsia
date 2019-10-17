// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SWAPCHAIN_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SWAPCHAIN_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

// Swapchain-related data. Usage is the following:
//
// 1) Create a VkSurfaceKHR then call vk_swapchain_create(...) with appropriate
//    configuration arguments passed as a vk_swapchain_config_t pointer.
//
// 2) Retrieve swapchain surface information with
//    vk_swapchain_get_surface_info() or one of the individual getter
//    functions (e.g. vk_swapchain_get_image_count(),
//    vk_swapchain_get_image_view(), etc).
//
// 3) Call vk_swapchain_enable_image_command_buffers() to let the swapchain
//    create one command buffer per swapchain image, which can later be
//    retrieved through vk_swapchain_get_image_command_buffer(). Note that
//    these are created for the graphics queue.
//
//    NOTE: This is optional, and provided as a convenience. The caller can
//    also chose to create its own set of command buffers instead.
//
// 4) Call vk_swapchain_enable_image_framebuffers() to let the swapchain
//    create one VkFramebuffer per swapchain image, each one of them associated
//    with the same render pass.
//
//    NOTE: This is also provided as a convenience. The caller can chose to
//    create its own framebuffers created from the results of
//    vk_swapchain_get_image_view() instead.
//
// 5) For every frame that needs to be presented:
//
//    a) First call vk_swapchain_prepare_next_image() which will return the
//       index of the next swapchain image to render to. The image and/or its
//       image view can be retrieved with vk_swapchain_get_image().
//
//    b) Render into the image with whatever means necessary. For example
//       by using the command buffer and framebuffers enabled in steps 3)
//       and 4) above.
//
//    c) Call either vk_swapchain_submit_image() or
//       vk_swapchain_submit_image_with_buffers().
//
//       The first one uses the default command buffers enabled through
//       vk_swapchain_enable_image_command_buffers() (which you should have
//       setup yourself before).
//
//       The second one allows you to use your own command buffers.
//
//       Both calls perform a vkQueueSubmit() on the graphics queue that
//       will properly synchronize with the presentation engine and the
//       swapchain image.
//
//    d) Call vk_swapchain_present_image() to send a presentation request
//       to the presentation engine for the current image.
//
//   Note that the implementation supports, for maximum throughput, several
//   frames being rendered concurrently through the GPU, using the |max_frames|
//   parameter in vk_swapchain_config_t. This corresponds to the FRAME_LAG
//   constant in the Vulkan Tutorial / Vulkan cube example programs.
//
// See the vk_triangle_test and vk_transfer_test examples for usage exmaples.
//
typedef struct vk_swapchain_t vk_swapchain_t;

typedef struct
{
  // The Vulkan instance, device, physical device, device and allocator to use.
  VkInstance                    instance;
  VkDevice                      device;
  VkPhysicalDevice              physical_device;
  const VkAllocationCallbacks * allocator;

  // Queue family and index to be used for presentation.
  uint32_t present_queue_family;
  uint32_t present_queue_index;

  // queue family and index to be used for graphics.
  uint32_t graphics_queue_family;
  uint32_t graphics_queue_index;

  // TODO(digit): Support graphics_queue_family != present_queue_family

  // The target presentation surface to use and its extent.
  VkSurfaceKHR surface_khr;

  // Maximum number of inflight frames to send to the swapchain.
  // This should be at least 1, and will be capped by the max number of
  // swapchain images supported by the surface / presentation engine.
  uint32_t max_frames;

  // Favorite surface pixel format. If not 0, the swapchain will try to
  // use this when creating the swapchain images. Check the results by
  // looking at |surface_format.format| after swapchain creation.
  VkFormat pixel_format;

  // TODO(digit): Provide a way to suggest a favorite surface format.
  // TODO(digit): Provide a way to suggest a favorite presentation mode.
  // TODO(digit): Provide a way to provide an old swapchain to support resizes.

} vk_swapchain_config_t;

// Create a new vk_swapchain_t instance. On success, returns a pointer
// to the new instance. On failure, aborts with an error message on stderr
// explaining the issue.
extern vk_swapchain_t *
vk_swapchain_create(const vk_swapchain_config_t * config);

// Print swapchain details to stdout. Useful for debugging.
extern void
vk_swapchain_print(const vk_swapchain_t * swapchain);

// Destroy a given swapchain instance. This will also destroy any optional
// command buffers and framebuffers enabled with vk_swapchain_enable_image_xxx().
extern void
vk_swapchain_destroy(vk_swapchain_t * swapchain);

// Information about the swapchain's surface returned by
// vk_swapchain_get_surface_info().
//
// |surface_extent| is the presentation surface's extent.
// |surface_format| is its pixel format and colorspace.
// |image_count| is the number of swapchain images.
// |frame_count| is the max number of frames that can be pushed to the GPU
// concurrently, and will be the maximum of either |image_count| or the
// |config.max_frames| value passed when calling vk_swapchain_create().
typedef struct
{
  VkExtent2D         surface_extent;
  VkSurfaceFormatKHR surface_format;
  uint32_t           image_count;
  uint32_t           frame_count;
} vk_swapchain_surface_info_t;

// Retrieve surface-specific information as a struct.
// Alternatively, use single-return helpers below.
extern vk_swapchain_surface_info_t
vk_swapchain_get_surface_info(const vk_swapchain_t * swapchain);

extern VkExtent2D
vk_swapchain_get_extent(const vk_swapchain_t * swapchain);

extern VkSurfaceFormatKHR
vk_swapchain_get_format(const vk_swapchain_t * swapchain);

extern uint32_t
vk_swapchain_get_image_count(const vk_swapchain_t * swapchain);

extern uint32_t
vk_swapchain_get_frame_count(const vk_swapchain_t * swapchain);

// Retrieve the VkSwapchainKHR value used by this vk_swapchain_t instance.
// Should only be useful for debugging.
extern VkSwapchainKHR
vk_swapchain_get_swapchain_khr(const vk_swapchain_t * swapchain);

// Information associated with each swapchain image, as
// returned by vk_swapchain_get_image_data().
//
// |image| and |image_view| are the corresponding VkImage and VkImageView,
// which are retrieved and created automatically by the swapchain instance.
//
// |command_buffer| is VK_NULL_HANDLE unless
// vk_swapchain_enable_image_command_buffers() is called (see related doc).
//
// |framebuffer| is VK_NULL_HANDLE unless
// vk_swapchain_enable_image_framebuffers() is called (see related doc).
//
typedef struct
{
  VkImage         image;
  VkImageView     image_view;
  VkCommandBuffer command_buffer;
  VkFramebuffer   framebuffer;
} vk_swapchain_image_data_t;

// Retrieve swapchain image-specific data as a struct.
// Alternatively, use the single-return helpers below to get the same values.
// IMPORTANT: |image_index| should be less than |surface_info.image_count| or
// the function will abort.
extern vk_swapchain_image_data_t
vk_swapchain_get_image_data(const vk_swapchain_t * swapchain, uint32_t image_index);

extern VkImage
vk_swapchain_get_image(const vk_swapchain_t * swapchain, uint32_t image_index);

extern VkImageView
vk_swapchain_get_image_view(const vk_swapchain_t * swapchain, uint32_t image_index);

// Create a set of command buffers for the graphics queue, one per swapchain image.
// These can later be retrieved with vk_swapchain_enable_image_command_buffers(),
// and will be deallocated automatically by vk_swapchain_destroy().
//
// NOTE: It is up to the caller to actually setup (i.e. record commands in) these
// buffers.
extern void
vk_swapchain_enable_image_command_buffers(vk_swapchain_t * swapchain,
                                          uint32_t         graphics_queue_family,
                                          uint32_t         graphics_queue_index);

// Retrieve the VkQueue created by a call to vk_swapchain_enable_image_command_buffers()
// or VK_NULL_HANDLE otherwise.
extern VkQueue
vk_swapchain_get_graphics_queue(const vk_swapchain_t * swapchain);

// Retrieve the command buffer associated with swapchain image identified by |image_index|.
// Aborts if |image_index| is invalid. Returns VK_NULL_HANDLE if vk_swapchain_enable_image_command_buffers()
// was not called.
extern VkCommandBuffer
vk_swapchain_get_image_command_buffer(const vk_swapchain_t * swapchain, uint32_t image_index);

// Create a set of framebuffers, one per swapchain image, each one of them
// being associated with the same |render_pass|. These can later be retrieved
// with vk_swapchain_enable_image_framebuffers(), and will be destroyed
// automatically by vk_swapchain_destroy().
extern void
vk_swapchain_enable_image_framebuffers(vk_swapchain_t * swapchain, VkRenderPass render_pass);

// Retrieve the framebuffer associated with a given swapchain image.
// Aborts if |image_index| is invalid. Returns VK_NULL_HANDLE if
// vk_swapchain_enable_image_framebuffers() was never called.
extern VkFramebuffer
vk_swapchain_get_image_framebuffer(const vk_swapchain_t * swapchain, uint32_t image_index);

// Retrieve a new swapchain image index. This only blocks
// On success, returns true and sets |*image_index| to a valid swapchain index
// and |*command_buffer| to the corresponding command buffer.
// On failure (i.e. window resizing), return false.
extern bool
vk_swapchain_prepare_next_image(vk_swapchain_t * swapchain, uint32_t * image_index);

// Submit the current swapchain image's, using the corresponding command buffer
// that is normally returned by vk_swapchain_get_image_command_buffer().
// NOTE: Callers should use vk_swapchain_submit_image_with_buffers() if a
// different set of command buffers is needed for this image.
extern void
vk_swapchain_submit_image(vk_swapchain_t * swapchain);

// Tell the swapchain to present the current image. On success, return true,
// false otherwise (i.e. if the window was resized).
extern bool
vk_swapchain_present_image(vk_swapchain_t * swapchain);

////////////////////////////////////////////////////////////////////////////
//
// NOTE: The following functions are useful if one does *not* want to use
// use vk_swapchain_submit_image(). This can happen if one wants to send
// additional command buffers to the graphics queue (e.g. when using Skia).
//
// A good understanding of how Vulkan swapchain synchronization works is
// recommended before using these!!
//
// IMPORTANT: The current swapchain image changes every time you call
// vk_swapchain_prepare_next_image(). This will affect the values returned
// by the functions below!

// Return the fence that is signaled to indicate rendering operations on the
// current swapchain image can start. This is waited on, and the reset, by
// vk_swapchain_prepare_next_image() before acquiring the image. It is also
// used by vk_swapchain_submit_image(). If the latter is not used, it should be
// signaled by the client, for example by using it in a vkQueueSubmit() call.
//
extern VkFence
vk_swapchain_get_image_rendered_fence(const vk_swapchain_t * swapchain);

// Return the semaphore used to wait for the current swapchain image acquisition.
// This is the semaphore that vk_swapchain_submit_image() will use for waiting.
extern VkSemaphore
vk_swapchain_get_image_acquired_semaphore(const vk_swapchain_t * swapchain);

// Return the semaphore used to signal rendering completion for the current
// swapchain image. This is the semaphore that is waited on by
// vk_swapchain_present_image(), and which is also signaled by
// vk_swapchain_submit_image().
extern VkSemaphore
vk_swapchain_get_image_rendered_semaphore(const vk_swapchain_t * swapchain);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SWAPCHAIN_H_
