// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_XCB_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_XCB_H_

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
// NOTE(allanmac): this jams XCB window and VK surface creation into
// one function -- consider splitting these further if there is a need.
//

struct surface *
surface_xcb_create(VkInstance                    vk_i,
                   VkAllocationCallbacks const * vk_ac,
                   VkRect2D const *              win_size,
                   char const *                  win_title);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_XCB_H_
