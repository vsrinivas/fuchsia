// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_STYLING_IMPL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_STYLING_IMPL_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "styling.h"

//
//
//

struct spn_device;
struct spn_vk_ds_styling_t;

//
//
//

spn_result
spn_styling_impl_create(struct spn_device * const   device,
                        struct spn_styling ** const styling,
                        uint32_t const              dwords_count,
                        uint32_t const              layers_count);

//
//
//

void
spn_styling_impl_pre_render_ds(struct spn_styling * const         styling,
                               struct spn_vk_ds_styling_t * const ds,
                               VkCommandBuffer                    cb);

void
spn_styling_impl_pre_render_wait(struct spn_styling * const   styling,
                                 uint32_t * const             waitSemaphoreCount,
                                 VkSemaphore * const          pWaitSemaphores,
                                 VkPipelineStageFlags * const pWaitDstStageMask);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_STYLING_IMPL_H_
