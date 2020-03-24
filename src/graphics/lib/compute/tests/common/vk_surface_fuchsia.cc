// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_surface.h"
#include "vk_utils.h"

// "vk_surface.h" only includes <vulkan/vulkan_core.h> and
// we need the Fuchsia-specific declarations as well here.
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

//
//
//

bool
vk_physical_device_supports_presentation(VkInstance       instance,
                                         VkPhysicalDevice physical_device,
                                         uint32_t         queue_family_index)
{
  // On Fuchsia, all physical devices support presentation and there is
  // no Vulkan extension (yet?) to query support for it anyway.
  return true;
}

vk_surface_requirements_t
vk_surface_get_requirements(bool disable_vsync)
{
  vk_surface_requirements_t reqs = {
    .num_layers     = 1,
    .num_extensions = 1,
  };

  const char * layer_name = disable_vsync ? "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb_skip_present"
                                          : "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";

#if MAX_VK_SURFACE_REQUIREMENTS_STORAGE < 2
#error "Please increment MAX_VK_SURFACE_REQUIREMENTS_STORAGE to at least 2"
#endif

  auto * storage = reinterpret_cast<const char **>(reqs.storage);
  storage[0]     = layer_name;
  storage[1]     = VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME;

  reqs.layer_names     = storage;
  reqs.extension_names = storage + 1;

  return reqs;
}

//
// vk_surface_t
//

struct vk_surface
{
  VkSurfaceKHR                  surface_khr;
  VkInstance                    instance;
  const VkAllocationCallbacks * allocator;
};

vk_surface_t *
vk_surface_create(const vk_surface_config_t * config)
{
#define DEFINE_VULKAN_ENTRY_POINT(instance_, name_)                                                \
  PFN_##name_ name_ = (PFN_##name_)vkGetInstanceProcAddr(instance_, #name_);                       \
  if (!name_)                                                                                      \
    {                                                                                              \
      fprintf(stderr, "ERROR: Missing %s Vulkan entry point!\n", #name_);                          \
    }

  DEFINE_VULKAN_ENTRY_POINT(config->instance, vkCreateImagePipeSurfaceFUCHSIA)
  if (!vkCreateImagePipeSurfaceFUCHSIA)
    return NULL;

#undef DEFINE_VULKAN_ENTRY_POINT

  uint32_t window_width  = config->window_width;
  uint32_t window_height = config->window_height;
  if (!window_width)
    window_width = 32;
  if (!window_height)
    window_height = 32;

  VkImagePipeSurfaceCreateInfoFUCHSIA const surface_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
    .pNext = NULL,
  };
  VkSurfaceKHR surface_khr = VK_NULL_HANDLE;
  VkResult     ret         = vkCreateImagePipeSurfaceFUCHSIA(config->instance,
                                                 &surface_info,
                                                 config->allocator,
                                                 &surface_khr);
  if (ret != VK_SUCCESS)
    {
      fprintf(stderr,
              "ERROR: Could not create Vulkan presentation surface: %s\n",
              vk_result_to_string(ret));
      return NULL;
    }

  vk_surface_t * result = reinterpret_cast<vk_surface_t *>(malloc(sizeof(*result)));
  *result               = (vk_surface_t){
    .surface_khr = surface_khr,
    .instance    = config->instance,
    .allocator   = config->allocator,
  };
  return result;
}

VkSurfaceKHR
vk_surface_get_surface_khr(const vk_surface_t * surface)
{
  return surface->surface_khr;
}

bool
vk_surface_poll_events(vk_surface_t * surface)
{
  // TODO(digit): Find a way to receive events from the system.
  return true;
}

void
vk_surface_destroy(vk_surface_t * surface)
{
  if (surface)
    {
      if (surface->surface_khr != VK_NULL_HANDLE)
        {
          vkDestroySurfaceKHR(surface->instance, surface->surface_khr, surface->allocator);
          surface->surface_khr = VK_NULL_HANDLE;
          surface->instance    = VK_NULL_HANDLE;
          surface->allocator   = NULL;
        }
      free(surface);
    }
}
