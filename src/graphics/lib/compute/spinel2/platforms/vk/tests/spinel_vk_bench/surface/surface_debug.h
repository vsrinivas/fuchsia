// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_SURFACE_DEBUG_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_SURFACE_DEBUG_H_

//
//
//

#include <stdio.h>
#include <vulkan/vulkan_core.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Pretty print formats
//

void
surface_debug_surface_formats(FILE *                     file,
                              uint32_t                   surface_format_count,
                              VkSurfaceFormatKHR const * surface_formats);

void
surface_debug_image_view_format(FILE * stderr, VkFormat image_view_format);

//
//
//

void
surface_debug_surface_transforms(FILE * file, VkSurfaceTransformFlagsKHR transform_flags);

void
surface_debug_composite_alphas(FILE * file, VkCompositeAlphaFlagsKHR alpha_flags);

void
surface_debug_image_usages(FILE * file, VkImageUsageFlags usage_flags);

//
//
//

void
surface_debug_surface_capabilities(FILE * file, VkSurfaceCapabilitiesKHR const * const sc);

//
//
//

void
surface_debug_surface_present_modes(FILE *                   file,
                                    uint32_t                 present_mode_count,
                                    VkPresentModeKHR const * present_modes);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_SURFACE_DEBUG_H_
