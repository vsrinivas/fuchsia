// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_STYLING_IMPL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_STYLING_IMPL_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "device.h"
#include "shaders/push.h"
#include "styling.h"

//
//
//

spinel_result_t
spinel_styling_impl_create(struct spinel_device *               device,
                           spinel_styling_create_info_t const * create_info,
                           spinel_styling_t *                   styling);

//
// Rendering currently requires retain/lock'ing the styling.
//
spinel_deps_immediate_semaphore_t
spinel_styling_retain_and_lock(struct spinel_styling * styling);

void
spinel_styling_unlock_and_release(struct spinel_styling * styling);

//
// Initialize RENDER push constants with styling bufrefs
//
void
spinel_styling_push_render_init(struct spinel_styling *     styling,
                                struct spinel_push_render * push_render);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_STYLING_IMPL_H_
