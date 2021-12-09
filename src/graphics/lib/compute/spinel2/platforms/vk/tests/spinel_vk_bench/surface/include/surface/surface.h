// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "surface_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Return the Vulkan surface handle.
//
VkSurfaceKHR
surface_to_vk(struct surface * surface);

//
// Dispose of the platform-specific surface state from the Vulkan
// instance.
//
// This will implicitly call `surface_detach()`.
//
// Note that a VkDeviceWaitIdle() may be called by this function.
//
void
surface_destroy(struct surface * surface);

//
// Attach the surface to a Vulkan device.
//
// Success:
//
//   VK_SUCCESS
//
// Failure:
//
//   VK_ERROR_OUT_OF_HOST_MEMORY
//   VK_ERROR_OUT_OF_DEVICE_MEMORY
//   VK_ERROR_DEVICE_LOST
//   VK_ERROR_SURFACE_LOST
//
VkResult
surface_attach(struct surface *           surface,
               VkPhysicalDevice           vk_pd,
               VkDevice                   vk_d,
               VkBool32                   is_fence_acquired,
               VkSurfaceFormatKHR const * surface_format,
               uint32_t                   min_image_count,
               VkExtent2D const *         max_image_extent,
               VkImageUsageFlags          image_usage,
               VkFormat                   image_view_format,
               VkComponentMapping const * image_view_components,
               VkPresentModeKHR           present_mode
               /* size_t payload_size    */
               /* size_t payload_alignof */);

//
// Detach all surface state associated with the Vulkan device.
//
// Note that if the VkDevice is lost then the surface must be detached
// with this function.
//
void
surface_detach(struct surface * surface);

//
// Regenerate the swapchain.
//
// If `surface_attach()` hasn't seen successfully invoked on this surface
// instance then VK_DEVICE_LOST will be returned.
//
// A non-NULL `extent` will be initialized with the new swapchain
// dimensions but is only valid if regeneration was successful.
//
// Success:
//   VK_SUCCESS
//
// Failure:
//   VK_ERROR_OUT_OF_HOST_MEMORY
//   VK_ERROR_OUT_OF_DEVICE_MEMORY
//   VK_ERROR_DEVICE_LOST
//   VK_ERROR_SURFACE_LOST_KHR
//   VK_ERROR_NATIVE_WINDOW_IN_USE_KHR
//   VK_ERROR_INITIALIZATION_FAILED
//
VkResult
surface_regen(struct surface * surface, VkExtent2D * extent, uint32_t * image_count);

//
// Returns the VkFence object that will be assigned to the next
// surface_acquire() invocation.
//
// Obtaining the VkFence object before acquiring a presentable may be
// necessary on platforms where vkAcquireNextImageKHR() never blocks.
//
// If `surface_attach()` hasn't seen successfully invoked on this surface
// instance then VK_DEVICE_LOST will be returned.
//
// If the `surface_attach()` argument `is_fence_acquired` was false then
// VK_ERROR_INITIALIZATION_FAILED will be returned.
//
// If VK_ERROR_OUT_OF_DATE_KHR is returned, there is no swapchain.
//
// Success:
//   VK_SUCCESS
//
// Failure:
//   VK_ERROR_INITIALIZATION_FAILED
//   VK_ERROR_OUT_OF_DATE_KHR;
//   VK_DEVICE_LOST
//
VkResult
surface_next_fence(struct surface * surface, VkFence * fence);

//
// Acquires the next presentable image and associated resources.
//
// If VK_ERROR_OUT_OF_DATE_KHR is returned, the swapchain will need to
// be regenerated.
//
// If `surface_attach()` hasn't seen successfully invoked on this surface
// instance then VK_DEVICE_LOST will be returned.
//
// If the `surface_attach()` argument `is_fence_acquired` was true then the
// host *must* ensure the fence was signaled before invoking this
// function.
//
// Otherwise, the errors that may be returned include:
//
// Success:
//   VK_SUCCESS
//   VK_TIMEOUT
//   VK_NOT_READY
//   VK_SUBOPTIMAL_KHR
//
// Failure:
//   VK_ERROR_OUT_OF_HOST_MEMORY
//   VK_ERROR_OUT_OF_DEVICE_MEMORY
//   VK_ERROR_DEVICE_LOST
//   VK_ERROR_OUT_OF_DATE_KHR
//   VK_ERROR_SURFACE_LOST_KHR
//   VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
//
VkResult
surface_acquire(struct surface *                    surface,  //
                uint64_t                            timeout_ns,
                struct surface_presentable const ** presentable,
                void *                              payload);

//
// Invokes input_pfn until idle
//
void
surface_input(struct surface * surface, surface_input_pfn_t input_pfn, void * data);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_H_
