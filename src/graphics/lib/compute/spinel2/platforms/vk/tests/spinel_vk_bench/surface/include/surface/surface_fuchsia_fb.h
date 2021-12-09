// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_FUCHSIA_FB_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_FUCHSIA_FB_H_

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

struct surface *
surface_fuchsia_create(VkInstance vk_i, VkAllocationCallbacks const * vk_ac);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_FUCHSIA_FB_H_
