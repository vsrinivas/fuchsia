// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SURFACE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SURFACE_H_

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

// Return true if a given physical device supports presentation, false
// otherwise.
extern bool
vk_physical_device_supports_presentation(VkInstance       instance,
                                         VkPhysicalDevice physical_device,
                                         uint32_t         queue_family_index);

#define MAX_VK_SURFACE_REQUIREMENTS_STORAGE 8

// Grab Vulkan instance requirements for presentation surface.
typedef struct vk_surface_requirements
{
  uint32_t             num_layers;
  uint32_t             num_extensions;
  const char * const * layer_names;
  const char * const * extension_names;

  // Free storage available for implementations to use (e.g. to store
  // layer/extension name pointers).
  uintptr_t storage[MAX_VK_SURFACE_REQUIREMENTS_STORAGE];

  // Optional function to be called to release this object's memory.
  // If |storage| was not large enough for the implementation.
  void (*free_func)(struct vk_surface_requirements * requirements);

} vk_surface_requirements_t;

extern vk_surface_requirements_t
vk_surface_get_requirements(bool disable_swapchain_present);

//
// vk_surface_t
//

// An opaque struct modelling a Vulkan presentation surface.
typedef struct vk_surface vk_surface_t;

// Configuration structure for vk_surface_t creation.
// |instance| is the Vulkan instance.
// |physical_device| is the physical device.
// |queue_family_index| is the queue family used for presentation.
// |allocator| is an optional Vulkan allocator pointer.
// |window_width| and |window_height| are the desired display surface
// dimensions. Only used as a hint since the implementation might
// enforce different dimensions (e.g. fullscreen framebuffers) or
// adjust the values (e.g. rounding to multiples of 4 or 8). A value
// of 0 means an arbitrary default (e.g. 32x32).
// |window_title| is an optional window title to use.
typedef struct
{
  VkInstance                    instance;
  VkPhysicalDevice              physical_device;
  uint32_t                      queue_family_index;
  const VkAllocationCallbacks * allocator;
  uint32_t                      window_width;
  uint32_t                      window_height;
  const char *                  window_title;
} vk_surface_config_t;

// Create a new presentaiton surface handle. On success, return a non-null
// pointer. On failure, return NULL after printing an error message to stderr()
// explaining the problem.
extern vk_surface_t *
vk_surface_create(const vk_surface_config_t * config);

// Return the Vulkan handle for this presentation surface.
extern VkSurfaceKHR
vk_surface_get_surface_khr(const vk_surface_t * surface);

// Poll input user events on this presentation surface.
// Return true on success, false if the program should quit.
// This function should be called before any draw call.
extern bool
vk_surface_poll_events(vk_surface_t * surface);

// Destroy a given presentation surface.
extern void
vk_surface_destroy(vk_surface_t * surface);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_SURFACE_H_
