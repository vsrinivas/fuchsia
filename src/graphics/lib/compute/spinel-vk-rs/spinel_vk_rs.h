// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_RS_SYS_SPINEL_VK_RS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_RS_SYS_SPINEL_VK_RS_H_

//
// Simplify creation of the Vulkan and Spinel objects used by spinel-rs-sys.
//
// TODO(allanmac): Integration with Carnelian can determine what internal Vulkan
// objects must be made available to Carnelian.
//

#include <vulkan/vulkan_core.h>

#include "spinel/spinel.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Opaque spinel_vk_rs type
//
typedef struct spinel_vk_rs spinel_vk_rs_t;

//
// Create a Vulkan instance
//
// Returns VK_NULL_HANDLE if return result is not equal to VK_SUCCESS.
//
// It is the responsibility of caller to destroy the VkInstance but not until
// all child objects created using the VkInstance must have been destroyed.
//
typedef struct spinel_vk_rs_instance_create_info
{
  bool is_validation;  // Enable validation
  bool is_debug_info;  // Enable debug object naming
} spinel_vk_rs_instance_create_info_t;

VkResult
spinel_vk_rs_instance_create(spinel_vk_rs_instance_create_info_t const * instance_create_info,
                             VkInstance *                                instance);

//
// Helper function to list physical device properties without creating a Vulkan
// instance.
//
// If `props` is NULL, then the number of physical devices available is returned
// in `props_count`.  Otherwise, `props_count` must point to a variable set by
// the user to the number of elements in the `props` array, and on return the
// variable is overwritten with the number of VkPhysicalDeviceProperties
// structures actually written to `props`. If `props_count` is less than the
// number of physical devices available, at most `props_count` structures will
// be written, and VK_INCOMPLETE will be returned instead of VK_SUCCESS, to
// indicate that not all the available physical devices properties were returned.
//
VkResult
spinel_vk_rs_get_physical_device_props(VkInstance                   instance,
                                       uint32_t *                   props_count,
                                       VkPhysicalDeviceProperties * props);

//
// Create the Vulkan and Spinel state used by spinel-rs-sys
//
typedef struct spinel_vk_rs_create_info
{
  VkInstance instance;
  uint32_t   vendor_id;                // Will select first physical device if 0:0
  uint32_t   device_id;                // Will select first physical device if 0:0
  uint32_t   qfis[2];                  // Can default to {0,0} on all known devices
  uint64_t   context_block_pool_size;  // Block pool size in bytes
  uint32_t   context_handle_count;     // Handle count
} spinel_vk_rs_create_info_t;

spinel_vk_rs_t *
spinel_vk_rs_create(spinel_vk_rs_create_info_t const * create_info);

//
// Invoked when the rendering surface size is first known or has changed.
//
// Must be invoked at least once before the very first render.
//
void
spinel_vk_rs_regen(spinel_vk_rs_t * rs, uint32_t width, uint32_t height, uint32_t image_count);

//
// Render an image
//
typedef struct spinel_vk_rs_render_image_info
{
  uint32_t    image_index;
  VkImage     image;
  VkImageView image_view;

  struct
  {
    VkImageLayout prev;
    VkImageLayout curr;
  } layout;

  spinel_pixel_clip_t clip;
  bool                is_srgb;  // explicitly convert from linear to SRGB
} spinel_vk_rs_render_image_info_t;

void
spinel_vk_rs_render(spinel_vk_rs_t *                         rs,
                    spinel_styling_t                         styling,
                    spinel_composition_t                     composition,
                    spinel_vk_rs_render_image_info_t const * image_info);

//
// Destroy Vulkan and Spinel state
//
void
spinel_vk_rs_destroy(spinel_vk_rs_t * rs);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_RS_SYS_SPINEL_VK_RS_H_
