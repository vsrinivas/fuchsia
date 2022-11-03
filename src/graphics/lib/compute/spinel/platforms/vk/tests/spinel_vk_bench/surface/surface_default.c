// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "surface_default.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "surface_debug.h"

//
//
//

struct wait
{
  VkSemaphore semaphore;
  VkFence     fence;
  VkResult    result;
};

//
// device state
//

struct device
{
  // clang-format off
  struct
  {
    VkPhysicalDevice         pd;
    VkDevice                 d;
  } vk;

  VkBool32                   is_fence_acquired;

  VkExtent2D                 max_image_extent;
  uint32_t                   min_image_count;

  struct
  {
    VkImageUsageFlags        usage;
  } image;

  struct
  {
    VkComponentMapping       components;
    VkFormat                 format;
  } image_view;

  // NOTE(allanmac): this assumes format and color space won't change
  // when swapchain is out of date -- if not true then hoist to
  // presentables
  VkSurfaceFormatKHR         surface_format;

  VkPresentModeKHR           present_mode;
  // clang-format on

  //
  // swapchain state
  //
  struct
  {
    VkExtent2D                   extent;
    struct wait *                waits;
    struct surface_presentable * presentables;
    uint32_t                     wait_count;
    uint32_t                     wait_next;
    uint32_t                     image_count;
  } swapchain;
};

//
//
//

VkSurfaceKHR
surface_default_to_vk(struct surface * surface)
{
  return surface->vk.surface;
}

//
//
//

#ifndef NDEBUG

static bool
surface_verify_surface_format(VkSurfaceKHR               surface,
                              VkPhysicalDevice           vk_pd,
                              VkSurfaceFormatKHR const * surface_format)
{
  //
  // get physical device surface formats
  //
  uint32_t sf_count;

  vkGetPhysicalDeviceSurfaceFormatsKHR(vk_pd, surface, &sf_count, NULL);

  VkSurfaceFormatKHR * const sfs = MALLOC_MACRO(sizeof(*sfs) * sf_count);

  vkGetPhysicalDeviceSurfaceFormatsKHR(vk_pd, surface, &sf_count, sfs);

  //
  // dump surface formats
  //
#ifndef NDEBUG
  surface_debug_surface_formats(stderr, sf_count, sfs);
#endif

  //
  // linear search for a format
  //
  bool is_match = false;

  for (uint32_t ii = 0; ii < sf_count; ii++)
    {
      if (memcmp(sfs + ii, surface_format, sizeof(*sfs)) == 0)
        {
          is_match = true;
          break;
        }
    }

  free(sfs);

  return is_match;
}

#endif

//
//
//

static void
destroy_swapchain(struct surface * const surface)
{
  struct device * const device = surface->device;

  // anything to do?
  uint32_t const image_count = device->swapchain.image_count;

  if (image_count == 0)
    {
      return;
    }

  // drain device before destroying retired -- ignore results
  (void)vkDeviceWaitIdle(device->vk.d);

  uint32_t const wait_count = device->swapchain.wait_count;

  // destroy waits and preesntables
  for (uint32_t ii = 0; ii < wait_count; ii++)
    {
      struct wait * const w = device->swapchain.waits + ii;

      vkDestroySemaphore(device->vk.d, w->semaphore, surface->vk.ac);

      if (device->is_fence_acquired)
        {
          if ((w->result == VK_SUCCESS) || (w->result == VK_SUBOPTIMAL_KHR))
            {
              //
              // wait for fences
              //
              // NOTE(allanmac): `vkDeviceWaitIdle()` doesn't appear to
              // propagate fence signals to the host. Why?
              //
              (void)vkWaitForFences(device->vk.d, 1, &w->fence, true, UINT64_MAX);
            }

          vkDestroyFence(device->vk.d, w->fence, surface->vk.ac);
        }
    }

  // destroy waits and presentables
  for (uint32_t ii = 0; ii < image_count; ii++)
    {
      struct surface_presentable * const p = device->swapchain.presentables + ii;

      vkDestroySemaphore(device->vk.d, p->signal, surface->vk.ac);
      vkDestroyImageView(device->vk.d, p->image_view, surface->vk.ac);
    }

  // destroy first swapchain
  vkDestroySwapchainKHR(device->vk.d,  //
                        device->swapchain.presentables[0].swapchain,
                        surface->vk.ac);

  //
  // clean up
  //
  free(device->swapchain.waits);
  free(device->swapchain.presentables);

  //
  // we only need to set image_count
  //
  device->swapchain.image_count = 0;
}

//
// Regenerate the swapchain
//

VkResult
surface_default_regen(struct surface * surface, VkExtent2D * extent, uint32_t * image_count)
{
  struct device * const device = surface->device;

  // there must be a device created via attach()
  if (device == NULL)
    {
      return VK_ERROR_DEVICE_LOST;
    }

  //
  // get the current/min/max extents
  //
  VkSurfaceCapabilitiesKHR sc;

  vk(GetPhysicalDeviceSurfaceCapabilitiesKHR(device->vk.pd, surface->vk.surface, &sc));

  //
  // determine extent
  //
  // NOTE(allanmac): sc.maxImageExtent can be (0,0) e.g. if the window is minimized
  //
  device->swapchain.extent = (VkExtent2D){
    .width  = MIN_MACRO(uint32_t,
                       MIN_MACRO(uint32_t,
                                 MAX_MACRO(uint32_t,  //
                                           sc.currentExtent.width,
                                           sc.minImageExtent.width),
                                 sc.maxImageExtent.width),
                       device->max_image_extent.width),
    .height = MIN_MACRO(uint32_t,
                        MIN_MACRO(uint32_t,
                                  MAX_MACRO(uint32_t,  //
                                            sc.currentExtent.height,
                                            sc.minImageExtent.height),
                                  sc.maxImageExtent.height),
                        device->max_image_extent.height),
  };

  //
  // update extent
  //
  if (extent != NULL)
    {
      *extent = device->swapchain.extent;
    }

  // is there an active swapchain
  bool const is_active = (device->swapchain.image_count > 0);

  //
  // if new extent is (0,0) then destroy presentables
  //
  if ((device->swapchain.extent.width == 0) && (device->swapchain.extent.height == 0))
    {
      if (is_active)
        {
          destroy_swapchain(surface);
        }

      return VK_SUCCESS;
    }

  //
  // otherwise, retire the active swapchain
  //
  VkSwapchainKHR retired_swapchain =  //
    is_active                         //
      ? device->swapchain.presentables[0].swapchain
      : VK_NULL_HANDLE;

  //
  // do we need a mutable format swapchain?
  //
  VkFormat const view_formats[] = {

    device->surface_format.format,
    device->image_view.format
  };

  VkImageFormatListCreateInfoKHR const iflci = {

    .sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR,
    .pNext           = NULL,
    .viewFormatCount = ARRAY_LENGTH_MACRO(view_formats),
    .pViewFormats    = view_formats
  };

  bool const is_mutable_reqd = (device->image_view.format != device->surface_format.format);

  //
  // create VkSwapchainKHR
  //
  VkSwapchainCreateInfoKHR const sci_khr = {

    .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .pNext                 = is_mutable_reqd ? &iflci : NULL,
    .flags                 = is_mutable_reqd ? VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR : 0,
    .surface               = surface->vk.surface,
    .minImageCount         = MAX_MACRO(uint32_t, device->min_image_count, sc.minImageCount),
    .imageFormat           = device->surface_format.format,
    .imageColorSpace       = device->surface_format.colorSpace,
    .imageExtent           = device->swapchain.extent,
    .imageArrayLayers      = 1,
    .imageUsage            = device->image.usage,
    .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL,
    .preTransform          = sc.currentTransform,
    .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode           = device->present_mode,
    .clipped               = VK_TRUE,  // read-only & clipped by window manager
    .oldSwapchain          = retired_swapchain
  };

  VkSwapchainKHR active_swapchain;

  VkResult const result = vkCreateSwapchainKHR(device->vk.d,  //
                                               &sci_khr,
                                               surface->vk.ac,
                                               &active_swapchain);

  //
  // destroy existing presentables and the now retired swapchain
  //
  destroy_swapchain(surface);

  //
  // only continue if the swapchain was created
  //
  if (result != VK_SUCCESS)
    {
      return result;
    }

  //
  // get image count
  //
  uint32_t new_image_count;

  vk(GetSwapchainImagesKHR(device->vk.d,  //
                           active_swapchain,
                           &new_image_count,
                           NULL));

  if (image_count != NULL)
    {
      *image_count = new_image_count;
    }

  //
  // get images
  //
  VkImage images[new_image_count];

  vk(GetSwapchainImagesKHR(device->vk.d,  //
                           active_swapchain,
                           &new_image_count,
                           images));

  //
  // save image count
  //
  device->swapchain.image_count = new_image_count;

  //
  // allocate as many waits as in-flight images plus one that is work-in-progress
  //
  // NOTE(allanmac): This is *not* backed by concrete understanding of how all
  // Vulkan swapchain implementations yield new presentables.
  //
  uint32_t const wait_count = new_image_count + 1;

  // clang-format off
  device->swapchain.waits        = MALLOC_MACRO(sizeof(*device->swapchain.waits) * wait_count);
  device->swapchain.presentables = MALLOC_MACRO(sizeof(*device->swapchain.presentables) * new_image_count);
  device->swapchain.wait_count   = wait_count;
  device->swapchain.wait_next    = 0;
  // clang-format on

  //
  // initialize presentables
  //
  VkSemaphoreCreateInfo const sci = {

    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0
  };

  VkFenceCreateInfo const fci = {

    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .pNext = NULL,
    .flags = VK_FENCE_CREATE_SIGNALED_BIT
  };

  VkImageViewCreateInfo ivci = {

    .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext            = NULL,
    .flags            = 0,
    .viewType         = VK_IMAGE_VIEW_TYPE_2D,
    .format           = device->image_view.format,
    .components       = device->image_view.components,
    .subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                          .baseMipLevel   = 0,
                          .levelCount     = 1,
                          .baseArrayLayer = 0,
                          .layerCount     = 1 }
  };

  for (uint32_t ii = 0; ii < wait_count; ii++)
    {
      struct wait * const w = device->swapchain.waits + ii;

      // wait.semaphore
      vk(CreateSemaphore(device->vk.d, &sci, surface->vk.ac, &w->semaphore));

      // wait.fence
      if (device->is_fence_acquired)
        {
          vk(CreateFence(device->vk.d, &fci, surface->vk.ac, &w->fence));
        }
      else
        {
          w->fence = VK_NULL_HANDLE;
        }

      w->result = VK_SUCCESS;  // default is successfully signalled
    }

  for (uint32_t ii = 0; ii < new_image_count; ii++)
    {
      struct surface_presentable * const p = device->swapchain.presentables + ii;

      // signal semaphore
      vk(CreateSemaphore(device->vk.d, &sci, surface->vk.ac, &p->signal));

      // swapchain
      p->swapchain = active_swapchain;

      // image
      p->image = ivci.image = images[ii];

      // image view
      vk(CreateImageView(device->vk.d, &ivci, surface->vk.ac, &p->image_view));

      // image index
      p->image_index = ii;

      // acquire_count
      p->acquire_count = 0;
    }

  return VK_SUCCESS;
}

//
//
//

void
surface_default_detach(struct surface * const surface)
{
  if (surface->device == NULL)
    {
      return;
    }

  destroy_swapchain(surface);

  free(surface->device);

  surface->device = NULL;
}

//
//
//

#ifndef NDEBUG

static bool
surface_verify_present_mode(VkPresentModeKHR         present_mode,
                            uint32_t                 present_mode_count,
                            VkPresentModeKHR const * present_modes)
{
  for (uint32_t ii = 0; ii < present_mode_count; ii++)
    {
      if (present_modes[ii] == present_mode)
        {
          return true;
        }
    }

  return false;
}

#endif

//
//
//

VkResult
surface_default_attach(struct surface *           surface,
                       VkPhysicalDevice           vk_pd,
                       VkDevice                   vk_d,
                       VkBool32                   is_fence_acquired,
                       VkSurfaceFormatKHR const * surface_format,
                       uint32_t                   min_image_count,
                       VkExtent2D const *         max_image_extent,
                       VkImageUsageFlags          image_usage,
                       VkFormat                   image_view_format,
                       VkComponentMapping const * image_view_components,
                       VkPresentModeKHR           present_mode)
{
  assert(surface->device == NULL);

  //
  // NOTE(allanmac): These cursory checks shouldn't be performed
  // here. They're the responsibility of the caller.
  //
#ifndef NDEBUG
  //
  // verify physical device surface support
  //
  VkBool32 is_pd_supported;

  vk(GetPhysicalDeviceSurfaceSupportKHR(vk_pd,  //
                                        0,
                                        surface->vk.surface,
                                        &is_pd_supported));

  assert(is_pd_supported);

  //
  // verify that the requested surface format is supported
  //
  bool const is_surface_format_supported = surface_verify_surface_format(surface->vk.surface,  //
                                                                         vk_pd,
                                                                         surface_format);

  assert(is_surface_format_supported);

  //
  // verify surface supports desired usage
  //
  VkSurfaceCapabilitiesKHR sc;

  vk(GetPhysicalDeviceSurfaceCapabilitiesKHR(vk_pd, surface->vk.surface, &sc));

  surface_debug_surface_capabilities(stderr, &sc);

  //
  // verify image count is valid
  //
#ifndef NDEBUG
  if (min_image_count < sc.minImageCount)
    {
      fprintf(stderr,
              "WARNING: min_image_count(%u) < sc.minImageCount(%u)\n",
              min_image_count,
              sc.minImageCount);
    }
#endif

  min_image_count = MAX_MACRO(uint32_t,  // increase if too small
                              min_image_count,
                              sc.minImageCount);

  //
  // verify surface supports present mode
  //
  VkPresentModeKHR present_modes[6];
  uint32_t         present_mode_count = ARRAY_LENGTH_MACRO(present_modes);

  vk(GetPhysicalDeviceSurfacePresentModesKHR(vk_pd,
                                             surface->vk.surface,
                                             &present_mode_count,
                                             present_modes));

  surface_debug_surface_present_modes(stderr, present_mode_count, present_modes);

  assert(surface_verify_present_mode(present_mode, present_mode_count, present_modes));

  //
  // verify imaged usage is supported
  //
  bool const is_usage_supported = (sc.supportedUsageFlags & image_usage) == image_usage;

  assert(is_usage_supported);

  //
  // report image_view_format
  //
  surface_debug_image_view_format(stderr, image_view_format);
#endif

  //
  // otherwise, create the device
  //
  struct device * const device = MALLOC_MACRO(sizeof(*device));

  surface->device = device;

  device->vk.pd                  = vk_pd;
  device->vk.d                   = vk_d;
  device->is_fence_acquired      = is_fence_acquired;
  device->min_image_count        = min_image_count;
  device->max_image_extent       = *max_image_extent;
  device->image.usage            = image_usage;
  device->image_view.format      = image_view_format;
  device->image_view.components  = *image_view_components;
  device->surface_format         = *surface_format;
  device->present_mode           = present_mode;
  device->swapchain.extent       = (VkExtent2D){ 0, 0 };
  device->swapchain.presentables = NULL;
  device->swapchain.image_count  = 0;

  return VK_SUCCESS;
}

//
//
//

VkResult
surface_default_next_fence(struct surface * surface, VkFence * fence)
{
  struct device * const device = surface->device;

  // there must be a device created via attach()
  if (device == NULL)
    {
      return VK_ERROR_DEVICE_LOST;
    }

  // are fences being acquired?
  if (!device->is_fence_acquired)
    {
      return VK_ERROR_INITIALIZATION_FAILED;
    }

  // if there is no swapchain then fence is VK_NULL_HANDLE
  if (device->swapchain.image_count == 0)
    {
      *fence = VK_NULL_HANDLE;

      return VK_ERROR_OUT_OF_DATE_KHR;
    }
  else
    {
      uint32_t const wait_count = device->swapchain.wait_count;
      uint32_t const wait_next  = device->swapchain.wait_next;
      uint32_t const wait_idx   = wait_next % wait_count;
      *fence                    = device->swapchain.waits[wait_idx].fence;

      return VK_SUCCESS;
    }
}

//
//
//

VkResult
surface_default_acquire(struct surface *                    surface,  //
                        uint64_t                            timeout,
                        struct surface_presentable const ** presentable,
                        void *                              payload)
{
  struct device * const device = surface->device;

  // there must be a device created via attach()
  if (device == NULL)
    {
      return VK_ERROR_DEVICE_LOST;
    }

  //
  // if swapchain wasn't created then return VK_ERROR_OUT_OF_DATE_KHR
  //
  uint32_t const image_count = device->swapchain.image_count;

  if (image_count == 0)
    {
      return VK_ERROR_OUT_OF_DATE_KHR;
    }

  //
  // acquire a presentable
  //
  uint32_t const      wait_count = device->swapchain.wait_count;
  uint32_t const      wait_next  = device->swapchain.wait_next;
  uint32_t const      wait_idx   = wait_next % wait_count;
  struct wait * const wait       = device->swapchain.waits + wait_idx;

  // reset the fence
  if (device->is_fence_acquired)
    {
      vk(ResetFences(device->vk.d, 1, &wait->fence));
    }

  VkSwapchainKHR swapchain = device->swapchain.presentables[0].swapchain;
  uint32_t       image_index;

  wait->result = vkAcquireNextImageKHR(device->vk.d,  //
                                       swapchain,
                                       timeout,
                                       wait->semaphore,
                                       wait->fence,
                                       &image_index);

  // only continue on success
  switch (wait->result)
    {
      case VK_SUCCESS:
      case VK_SUBOPTIMAL_KHR:
        break;

      case VK_TIMEOUT:
      case VK_NOT_READY:
      case VK_ERROR_OUT_OF_HOST_MEMORY:
      case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      case VK_ERROR_DEVICE_LOST:
      case VK_ERROR_OUT_OF_DATE_KHR:
      case VK_ERROR_SURFACE_LOST_KHR:
      case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
        return wait->result;

      default:
        //
        // Note that there is an outstanding NVIDIA swapchain bug which
        // incorrectly returns VK_ERROR_VALIDATION_FAILED_EXT:
        //
        // https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/1363
        //
        // Otherwise, check to see if the spec has been updated!
        //
        // FIXME(allanmac): remove when fixed!
        //
        fprintf(stderr,
                "**\n"
                "** Invalid result from vkAcquireNextImageKHR(): %s\n"
                "**\n"
                "** Note that there is an outstanding NVIDIA swapchain bug that\n"
                "** results in the return of VK_ERROR_VALIDATION_FAILED_EXT.\n"
                "**\n"
                "** See: https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/1363\n"
                "**\n",
                vk_get_result_string(wait->result));
        exit(EXIT_FAILURE);
    };

  // only bump wait ring upon success
  device->swapchain.wait_next++;

  // get corresponding presentable
  struct surface_presentable * p = device->swapchain.presentables + image_index;

  //
  // update presentable with wait objects and save payload
  //
  p->wait.semaphore = wait->semaphore;
  p->wait.fence     = wait->fence;
  p->acquire_count  = p->acquire_count + 1;  // clang-format!
  p->payload        = payload;

  // return read-only copy
  *presentable = p;

  return wait->result;
}

//
//
//
