// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SWAPCHAIN_STAGING_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SWAPCHAIN_STAGING_H_

#include <vulkan/vulkan.h>

// Certain presentation surfaces do not support all the features required
// by clients (e.g. Intel does not support VK_IMAGE_USAGE_STORAGE_BIT).
//
// "Staging" refers to providing intermediates Vulkan images between
// the client and the real swapchain to support these use cases, i.e.:
//
typedef struct vk_swapchain_staging vk_swapchain_staging_t;

extern vk_swapchain_staging_t *
vk_swapchain_staging_create(uint32_t                      image_count,
                            uint32_t                      frame_count,
                            VkImageUsageFlags             wanted_usage,
                            VkFormat                      wanted_format,
                            VkExtent2D                    swapchain_extent,
                            VkFormat                      swapchain_format,
                            const VkImage *               swapchain_images,
                            VkDevice                      device,
                            VkPhysicalDevice              physical_device,
                            uint32_t                      present_queue_family,
                            uint32_t                      present_queue_index,
                            const VkAllocationCallbacks * allocator);

extern void
vk_swapchain_staging_destroy(vk_swapchain_staging_t * staging);

// Return staging surface format. This will match the |wanted_format| that was
// passed to vk_swapchain_staging_create().
extern VkSurfaceFormatKHR
vk_swapchain_staging_get_format(const vk_swapchain_staging_t * staging);

// Return staging image for |image_index|.
extern VkImage
vk_swapchain_staging_get_image(const vk_swapchain_staging_t * staging, uint32_t image_index);

// Return staging image view for |image_index|.
extern VkImageView
vk_swapchain_staging_get_image_view(const vk_swapchain_staging_t * staging, uint32_t image_index);

// Ensure that staging image identified by |image_index| is presented to the
// swapchain. This enqueues a command buffer with a submit that will wait on
// |wait_semaphore|, and which performs all necessary blits / transfers.
//
// |frame_index| is the corresponding frame number used for synchronization.
//
// Returns the semaphore that will be signalled when the submitted command
// buffer completes.
extern VkSemaphore
vk_swapchain_staging_present_image(vk_swapchain_staging_t * staging,
                                   uint32_t                 image_index,
                                   uint32_t                 frame_index,
                                   VkSemaphore              wait_semaphore);

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SWAPCHAIN_STAGING_H_
