// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_SURFACE_DEFAULT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_SURFACE_DEFAULT_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "surface/surface.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

struct surface
{
  // clang-format off
  struct surface_platform *       platform;

  struct
  {
    VkInstance                    i;
    VkAllocationCallbacks const * ac;
    VkSurfaceKHR                  surface;
  } vk;

  struct device *                 device;

  //
  //
  //

  VkSurfaceKHR (*to_vk)     (struct surface * surface);

  void         (*destroy)   (struct surface * surface);

  VkResult     (*attach)    (struct surface *           surface,
                             VkPhysicalDevice           vk_pd,
                             VkDevice                   vk_d,
                             VkBool32                   is_fence_acquired,
                             VkSurfaceFormatKHR const * surface_format,
                             uint32_t                   min_image_count,
                             VkExtent2D const *         max_image_extent,
                             VkImageUsageFlags          image_usage,
                             VkFormat                   image_view_format,
                             VkComponentMapping const * image_view_components,
                             VkPresentModeKHR           present_mode);

  void         (*detach)    (struct surface * surface);

  VkResult     (*regen)     (struct surface * surface,
                             VkExtent2D     * extent,
                             uint32_t       * image_count);

  VkResult     (*next_fence)(struct surface * surface, VkFence * fence);

  VkResult     (*acquire)   (struct surface *                    surface,
                             uint64_t                            timeout,
                             struct surface_presentable const ** presentable,
                             void *                              payload);

  void         (*input)     (struct surface *    surface,
                             surface_input_pfn_t input_pfn,
                             void *              data);
  // clang-format on
};

//
//
//

VkSurfaceKHR
surface_default_to_vk(struct surface * surface);

VkResult
surface_default_attach(struct surface *           surface,
                       VkPhysicalDevice           vk_pd,
                       VkDevice                   vk_d,
                       VkBool32                   is_fence_acquired,
                       VkSurfaceFormatKHR const * surface_format,
                       uint32_t                   min_image_count,
                       VkExtent2D const *         max_image_extent,
                       VkImageUsageFlags          image_usage,
                       VkFormat                   image_view_format,
                       VkComponentMapping const * image_view_components,
                       VkPresentModeKHR           present_mode);

VkResult
surface_default_regen(struct surface * surface, VkExtent2D * extent, uint32_t * image_count);

VkResult
surface_default_next_fence(struct surface * surface, VkFence * fence);

VkResult
surface_default_acquire(struct surface *                    surface,  //
                        uint64_t                            timeout,
                        struct surface_presentable const ** presentable,
                        void *                              payload);

void
surface_default_detach(struct surface * const surface);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_SURFACE_DEFAULT_H_
