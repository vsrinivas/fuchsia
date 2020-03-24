// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <GLFW/glfw3.h>
#include <stdio.h>

#include "vk_strings.h"
#include "vk_surface.h"

//
// GLFW setup
//

// Print GLFW errors to stderr to ease debugging.
static void
glfw_error_callback(int error, const char * message)
{
  fprintf(stderr, "GLFW:error=%d: %s\n", error, message);
}

static void
glfw_ensure_init()
{
  static bool init = false;
  if (!init)
    {
      glfwInit();

      glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
      //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

      glfwSetErrorCallback(glfw_error_callback);
      init = false;
    }
}

//
//
//

bool
vk_physical_device_supports_presentation(VkInstance       instance,
                                         VkPhysicalDevice physical_device,
                                         uint32_t         queue_family_index)
{
  return glfwGetPhysicalDevicePresentationSupport(instance, physical_device, queue_family_index) ==
         GLFW_TRUE;
}

vk_surface_requirements_t
vk_surface_get_requirements(bool disable_vsync)
{
  glfw_ensure_init();

  vk_surface_requirements_t reqs = {};

  // NOTE: Storage is provided by GLFW.
  reqs.extension_names = glfwGetRequiredInstanceExtensions(&reqs.num_extensions);

  if (disable_vsync)
    {
      fprintf(stderr, "WARNING: disable_swapchain_present is ignored on this platform!\n");
    }

  return reqs;
}

//
// vk_surface_t
//

struct vk_surface
{
  GLFWwindow *                  window;
  VkSurfaceKHR                  surface_khr;
  VkInstance                    instance;
  const VkAllocationCallbacks * allocator;
};

vk_surface_t *
vk_surface_create(const vk_surface_config_t * config)
{
  glfw_ensure_init();
  uint32_t     window_width  = config->window_width;
  uint32_t     window_height = config->window_height;
  const char * window_title  = config->window_title;

  if (!window_width)
    window_width = 32;
  if (!window_height)
    window_height = 32;
  if (!window_title)
    window_title = "Vulkan window";

  GLFWwindow * window = glfwCreateWindow(window_width, window_height, window_title, NULL, NULL);
  if (!window)
    {
      fprintf(stderr, "Could not create GLFW window!\n");
      return NULL;
    }
  VkSurfaceKHR surface_khr = VK_NULL_HANDLE;
  VkResult ret = glfwCreateWindowSurface(config->instance, window, config->allocator, &surface_khr);
  if (ret != VK_SUCCESS)
    {
      fprintf(stderr, "Could not create GLFW-backed Vulkan presentation surface!");
      glfwDestroyWindow(window);
      return NULL;
    }

  vk_surface_t * result = malloc(sizeof(*result));
  *result               = (vk_surface_t){
    .window      = window,
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
  GLFWwindow * window = surface->window;

  if (!window)
    return false;

  if (glfwWindowShouldClose(window))
    return false;

  glfwPollEvents();
  return true;
}

void
vk_surface_destroy(vk_surface_t * surface)
{
  if (!surface)
    return;

  if (surface->surface_khr != VK_NULL_HANDLE)
    {
      vkDestroySurfaceKHR(surface->instance, surface->surface_khr, surface->allocator);
      surface->surface_khr = VK_NULL_HANDLE;
      surface->instance    = VK_NULL_HANDLE;
      surface->allocator   = NULL;
    }

  if (surface->window)
    {
      glfwDestroyWindow(surface->window);
      surface->window = NULL;
    }

  free(surface);
}
