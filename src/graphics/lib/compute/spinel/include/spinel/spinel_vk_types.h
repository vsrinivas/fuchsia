// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_VK_TYPES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_VK_TYPES_H_

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
  SPN_RENDER_SUBMIT_EXT_TYPE_VK_IMAGE,
  SPN_RENDER_SUBMIT_EXT_TYPE_VK_BUFFER,
  SPN_RENDER_SUBMIT_EXT_TYPE_VK_COPY_BUFFER_TO_BUFFER,
  SPN_RENDER_SUBMIT_EXT_TYPE_VK_COPY_BUFFER_TO_IMAGE,
} spn_render_submit_ext_type_e;

//
// RENDER TO A VULKAN IMAGE
//

typedef struct spn_render_submit_ext_vk_image
{
  void *                       ext;
  spn_render_submit_ext_type_e type;
  VkDescriptorImageInfo        surface;
  VkSubmitInfo const *         si;  // FIXME(allanmac): about to change
} spn_render_submit_ext_vk_image_t;

//
// RENDER TO A VULKAN BUFFER
//

typedef struct spn_render_submit_ext_vk_buffer
{
  void *                       ext;
  spn_render_submit_ext_type_e type;
  VkDescriptorBufferInfo       surface;
  uint32_t                     surface_pitch;
  VkBool32                     clear;
  VkSubmitInfo const *         si;  // FIXME(allanmac): about to change
} spn_render_submit_ext_vk_buffer_t;

//
// COPY THE VULKAN BUFFER TO A BUFFER AFTER RENDERING
//

typedef struct spn_render_submit_ext_vk_copy_buffer_to_buffer
{
  void *                       ext;
  spn_render_submit_ext_type_e type;
  VkDescriptorBufferInfo       dst;
  VkDeviceSize                 dst_size;
} spn_render_submit_ext_vk_copy_buffer_to_buffer_t;

//
// COPY THE VULKAN BUFFER TO AN IMAGE AFTER RENDERING
//

typedef struct spn_render_submit_ext_vk_copy_buffer_to_image
{
  void *                       ext;
  spn_render_submit_ext_type_e type;
  VkImage                      dst;
  VkImageLayout                dst_layout;
  uint32_t                     region_count;
  const VkBufferImageCopy *    regions;
} spn_render_submit_ext_vk_copy_buffer_to_image_t;

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_VK_TYPES_H_
