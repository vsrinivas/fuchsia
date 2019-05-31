// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPINEL_VK_TYPES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPINEL_VK_TYPES_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// VK RENDER EXTENSIONS
//

typedef enum spn_render_submit_ext_type_e
{
  SPN_RENDER_SUBMIT_EXT_TYPE_VK_BUFFER,
  SPN_RENDER_SUBMIT_EXT_TYPE_VK_IMAGE,
} spn_render_submit_ext_type_e;

//
// RENDER TO A VULKAN BUFFER
//

typedef struct spn_render_submit_ext_vk_buffer
{
  void *                       ext;
  spn_render_submit_ext_type_e type;
  VkDescriptorBufferInfo       surface;
  uint32_t                     surface_pitch;
  VkSubmitInfo const *         si;
} spn_render_submit_ext_vk_buffer_t;

//
// RENDER TO A VULKAN IMAGE
//

typedef struct spn_render_submit_ext_vk_image
{
  void *                       ext;
  spn_render_submit_ext_type_e type;
  VkDescriptorImageInfo        surface;
  VkSubmitInfo const *         si;
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

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPINEL_VK_TYPES_H_
