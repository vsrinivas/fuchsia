// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_COMPOSITION_IMPL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_COMPOSITION_IMPL_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "composition.h"
#include "device.h"
#include "shaders/push.h"

//
//
//
spinel_result_t
spinel_composition_impl_create(struct spinel_device *       device,
                               struct spinel_composition ** composition);

//
// Rendering currently requires retain/lock'ing the composition.
//
spinel_deps_immediate_semaphore_t
spinel_composition_retain_and_lock(struct spinel_composition * composition);

void
spinel_composition_unlock_and_release(struct spinel_composition * composition);

//
//
//
void
spinel_composition_push_render_dispatch_record(struct spinel_composition * composition,
                                               VkCommandBuffer             cb);
//
// 1. Initialize RENDER push constants with composition bufrefs
// 2. Record composition-driven indirect dispatch command
//
void
spinel_composition_push_render_init_record(struct spinel_composition * composition,
                                           struct spinel_push_render * push_render,
                                           VkCommandBuffer             cb);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_COMPOSITION_IMPL_H_
