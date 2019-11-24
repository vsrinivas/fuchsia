// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_IMAGE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_IMAGE_H_

#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  VkImage                       image;
  VkDeviceMemory                memory;
  VkDeviceSize                  size;
  VkExtent2D                    extent;
  VkImageView                   image_view;
  VkDevice                      device;
  const VkAllocationCallbacks * allocator;

  // fields below are for debugging.
  VkMemoryRequirements memory_requirements;
  uint32_t             memory_type_index;
  VkImageTiling        tiling;
} vk_image_t;

// Generic image allocation function. Prefer calling convenience functions below.
// instead.
// NOTE: This function will abort with an error message is |image_tiling|,
// |image_usage| and |image_format| are not compatible for this device.
extern void
vk_image_alloc_generic(vk_image_t *                  image,
                       VkFormat                      image_format,
                       VkExtent2D                    image_extent,
                       VkImageTiling                 image_tiling,
                       VkImageUsageFlags             image_usage,
                       VkImageLayout                 image_layout,
                       VkMemoryPropertyFlags         memory_flags,
                       uint32_t                      queue_families_count,
                       const uint32_t *              queue_families,
                       VkPhysicalDevice              physical_device,
                       VkDevice                      device,
                       const VkAllocationCallbacks * allocator);

// Allocate a new device-local image.
// Note that this will try to use optimal tiling is |image_format| supports it,
// otherwise linear tiling will be used instead. The function also aborts with
// an error message is the format supports neither of these tilings.
extern void
vk_image_alloc_device_local(vk_image_t *                  image,
                            VkFormat                      image_format,
                            VkExtent2D                    image_extent,
                            VkImageUsageFlags             image_usage,
                            VkPhysicalDevice              physical_device,
                            VkDevice                      device,
                            const VkAllocationCallbacks * allocator);

extern void
vk_image_free(vk_image_t * image);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_IMAGE_H_
