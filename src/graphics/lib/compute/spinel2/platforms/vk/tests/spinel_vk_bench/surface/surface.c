// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "surface_default.h"

//
//
//

VkSurfaceKHR
surface_to_vk(struct surface * surface)
{
  return surface->to_vk(surface);
}

//
//
//

void
surface_destroy(struct surface * surface)
{
  surface->destroy(surface);
}

//
//
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
               VkPresentModeKHR           present_mode)
{
  return surface->attach(surface,
                         vk_pd,
                         vk_d,
                         is_fence_acquired,
                         surface_format,
                         min_image_count,
                         max_image_extent,
                         image_usage,
                         image_view_format,
                         image_view_components,
                         present_mode);
}

//
//
//

void
surface_detach(struct surface * surface)
{
  surface->detach(surface);
}

//
//
//

VkResult
surface_regen(struct surface * surface, VkExtent2D * extent, uint32_t * image_count)
{
  return surface->regen(surface, extent, image_count);
}

//
//
//

VkResult
surface_next_fence(struct surface * surface, VkFence * fence)
{
  return surface->next_fence(surface, fence);
}

//
//
//

VkResult
surface_acquire(struct surface *                    surface,  //
                uint64_t                            timeout,
                struct surface_presentable const ** presentable,
                void *                              payload)
{
  return surface->acquire(surface,  //
                          timeout,
                          presentable,
                          payload);
}

//
//
//

void
surface_input(struct surface * surface, surface_input_pfn_t input_pfn, void * data)
{
  surface->input(surface, input_pfn, data);
}

//
//
//
