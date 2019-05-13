// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel_types.h"



//
// VK RENDER EXTENSIONS
//

typedef enum spn_render_submit_ext_type_e
{
  SPN_RENDER_SUBMIT_EXT_TYPE_VK_BUFFER,
  SPN_RENDER_SUBMIT_EXT_TYPE_VK_IMAGE,
} spn_render_submit_ext_type_e;

//
// Render to a Vulkan buffer
//

struct spn_render_submit_ext_vk_buffer
{
  void                         * ext;
  spn_render_submit_ext_type_e   type;
  VkDescriptorBufferInfo         surface;
  uint32_t                       surface_pitch;
  VkSubmitInfo           const * si;
} spn_render_submit_ext_vk_buffer_t;

//
// Render to a Vulkan image
//

struct spn_render_submit_ext_vk_image
{
  void                         * ext;
  spn_render_submit_ext_type_e   type;
  VkDescriptorImageInfo          surface;
  VkSubmitInfo           const * si;
} spn_render_submit_ext_vk_image_t;

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//
