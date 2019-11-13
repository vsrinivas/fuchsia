// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_swapchain.h"

#include <stdio.h>
#include <stdlib.h>

#include "vk_format_matcher.h"
#include "vk_strings.h"
#include "vk_surface.h"
#include "vk_utils.h"

// Set to 1 to enable debug traces to stdout.
#define DEBUG_SWAPCHAIN 0

#if DEBUG_SWAPCHAIN
#include <unistd.h>
#define PRINT(...) (printf(__VA_ARGS__), fflush(stdout))
#define SLEEP(seconds) sleep(seconds)
#else
#define PRINT(...) ((void)0)
#define SLEEP(seconds) ((void)0)
#endif

#define PANIC(...) ASSERT_MSG(false, __VA_ARGS__)

//
//
//

// Maximum number of swapchain images
#define MAX_VK_SWAPCHAIN_IMAGES 8

struct vk_swapchain
{
  VkInstance                    instance;
  VkDevice                      device;
  const VkAllocationCallbacks * allocator;

  VkPhysicalDevice   physical_device;
  VkSurfaceKHR       surface_khr;
  VkSwapchainKHR     swapchain_khr;
  VkQueue            present_queue;
  VkSurfaceFormatKHR surface_format;
  VkExtent2D         surface_extent;
  VkPresentModeKHR   present_mode;
  VkCommandPool      present_command_pool;

  // The following arrays are indexed by swapchain image index.
  uint32_t        image_count;
  uint32_t        image_index;
  uint32_t        image_counter;
  bool            has_framebuffers;
  VkImage         images[MAX_VK_SWAPCHAIN_IMAGES];
  VkImageView     image_views[MAX_VK_SWAPCHAIN_IMAGES];
  VkCommandBuffer image_present_command_buffers[MAX_VK_SWAPCHAIN_IMAGES];

  // The following arrays are indexed by frame index.
  uint32_t    frame_count;
  uint32_t    frame_index;
  VkSemaphore frame_rendered_semaphores[MAX_VK_SWAPCHAIN_IMAGES];
  VkSemaphore frame_available_semaphores[MAX_VK_SWAPCHAIN_IMAGES];
  VkFence     frame_inflight_fences[MAX_VK_SWAPCHAIN_IMAGES];
#if DEBUG_SWAPCHAIN
  VkFence frame_acquired_fences[MAX_VK_SWAPCHAIN_IMAGES];
#endif
};

VkSwapchainKHR
vk_swapchain_get_swapchain_khr(const vk_swapchain_t * swapchain)
{
  return swapchain->swapchain_khr;
}

VkSurfaceFormatKHR
vk_swapchain_get_format(const vk_swapchain_t * swapchain)
{
  return swapchain->surface_format;
}

VkExtent2D
vk_swapchain_get_extent(const vk_swapchain_t * swapchain)
{
  return swapchain->surface_extent;
}

uint32_t
vk_swapchain_get_image_count(const vk_swapchain_t * swapchain)
{
  return swapchain->image_count;
}

uint32_t
vk_swapchain_get_frame_count(const vk_swapchain_t * swapchain)
{
  return swapchain->frame_count;
}

VkImage
vk_swapchain_get_image(const vk_swapchain_t * swapchain, uint32_t image_index)
{
  ASSERT_MSG(image_index < swapchain->image_count, "Invalid image index: %u\n", image_index);
  return swapchain->images[image_index];
}

VkImageView
vk_swapchain_get_image_view(const vk_swapchain_t * swapchain, uint32_t image_index)
{
  ASSERT_MSG(image_index < swapchain->image_count, "Invalid image index: %u\n", image_index);
  return swapchain->image_views[image_index];
}

// Create a new semaphore.
static VkSemaphore
vk_swapchain_create_semaphore(vk_swapchain_t * swapchain)
{
  VkSemaphore semaphore;

  vk(CreateSemaphore(swapchain->device,
                     &(const VkSemaphoreCreateInfo){
                       .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                     },
                     swapchain->allocator,
                     &semaphore));

  return semaphore;
}

// Create a new fence.
static VkFence
vk_swapchain_create_fence(vk_swapchain_t * swapchain, bool signalled)
{
  VkFence fence;

  const VkFenceCreateInfo fence_info = {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .flags = signalled ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
  };
  vk(CreateFence(swapchain->device, &fence_info, swapchain->allocator, &fence));
  return fence;
}

// Synchronously transition all swapchain images to a new layout.
static void
vk_swapchain_transition_image_layouts(vk_swapchain_t * swapchain,
                                      VkQueue          queue,
                                      VkCommandPool    command_pool,
                                      VkImageLayout    old_layout,
                                      VkImageLayout    new_layout)
{
  VkCommandBuffer cmd_buffer;
  vk(AllocateCommandBuffers(swapchain->device,
                            &(const VkCommandBufferAllocateInfo){
                              .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                              .commandPool        = command_pool,
                              .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              .commandBufferCount = 1,
                            },
                            &cmd_buffer));

  const VkCommandBufferBeginInfo beginInfo = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
  };
  vk(BeginCommandBuffer(cmd_buffer, &beginInfo));

  VkImageMemoryBarrier image_barriers[MAX_VK_SWAPCHAIN_IMAGES];

  for (uint32_t nn = 0; nn < swapchain->image_count; ++nn)
    {
      image_barriers[nn] = (const VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = 0,
        .oldLayout     = old_layout,
        .newLayout     = new_layout,
        .image         = swapchain->images[nn],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    }

  vkCmdPipelineBarrier(cmd_buffer,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       0,
                       0,
                       NULL,
                       0,
                       NULL,
                       swapchain->image_count,
                       image_barriers);

  vkEndCommandBuffer(cmd_buffer);

  VkFence fence = vk_swapchain_create_fence(swapchain, false);

  vk(QueueSubmit(queue,
                 1,
                 &(const VkSubmitInfo){
                   .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                   .commandBufferCount = 1,
                   .pCommandBuffers    = &cmd_buffer,
                 },
                 fence));

  vk(WaitForFences(swapchain->device, 1, &fence, VK_TRUE, UINT64_MAX));
  vkDestroyFence(swapchain->device, fence, swapchain->allocator);

  vkFreeCommandBuffers(swapchain->device, command_pool, 1, &cmd_buffer);
}

vk_swapchain_t *
vk_swapchain_create(const vk_swapchain_config_t * config)
{
  // Sanity check.
  {
    VkBool32 supported = 0;
    VkResult result    = vkGetPhysicalDeviceSurfaceSupportKHR(config->physical_device,
                                                           config->present_queue_family,
                                                           config->surface_khr,
                                                           &supported);

    ASSERT_MSG(result == VK_SUCCESS && supported == VK_TRUE,
               "This device does not support presenting to this surface!\n");
  }

  vk_swapchain_t * swapchain = calloc(1, sizeof(*swapchain));

  swapchain->instance        = config->instance;
  swapchain->device          = config->device;
  swapchain->physical_device = config->physical_device;
  swapchain->allocator       = config->allocator;
  swapchain->surface_khr     = config->surface_khr;
  swapchain->frame_count     = config->max_frames;

  // Grab surface info
  vk_device_surface_info_t surface_info;

  vk_device_surface_info_init(&surface_info,
                              config->physical_device,
                              config->surface_khr,
                              config->instance);

#if DEBUG_SWAPCHAIN
  vk_device_surface_info_print(&surface_info);
#endif

  // Format selection based on configuration.
  VkImageUsageFlags image_usage = config->image_usage_flags;
  if (!image_usage)
    image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  ASSERT_MSG((surface_info.capabilities.supportedUsageFlags & image_usage) == image_usage,
             "This device does not support presenting with %s (only %s)",
             vk_image_usage_flags_to_string(image_usage),
             vk_image_usage_flags_to_string(surface_info.capabilities.supportedUsageFlags));

  VkFormat format = vk_device_surface_info_find_presentation_format(&surface_info,
                                                                    image_usage,
                                                                    config->pixel_format);
  if (format == VK_FORMAT_UNDEFINED)
    {
      if (config->pixel_format == VK_FORMAT_UNDEFINED)
        {
          ASSERT_MSG(false,
                     "This device has no presentation format compatible with %s\n",
                     vk_image_usage_flags_to_string(image_usage));
        }
      else
        {
          ASSERT_MSG(false,
                     "This device does not support %s for pixel format %s\n",
                     vk_image_usage_flags_to_string(image_usage),
                     vk_format_to_string(config->pixel_format));
        }
    }

  swapchain->surface_format = (VkSurfaceFormatKHR){
    .format     = format,
    .colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
  };

  // Presentation mode selection.

  // TODO(digit): Allow the client to specify an alternative presentation mode.
  swapchain->present_mode =
    config->disable_vsync ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;

  uint32_t surface_image_count = surface_info.capabilities.minImageCount;

  // NOTE: A maxImageCount value of 0 means there is no limit in the number of swapchain images.
  if (swapchain->frame_count > surface_info.capabilities.maxImageCount &&
      surface_info.capabilities.maxImageCount != 0)
    {
      swapchain->frame_count = surface_info.capabilities.maxImageCount;
    }

  if (surface_info.capabilities.currentExtent.width == 0xffffffffu)
    {
      swapchain->surface_extent = surface_info.capabilities.minImageExtent;
    }
  else
    {
      swapchain->surface_extent = surface_info.capabilities.currentExtent;
    }

  vk_device_surface_info_destroy(&surface_info);

  // TODO: Support graphics_queue_family != present_queue_family
  ASSERT_MSG(config->graphics_queue_family == config->present_queue_family,
             "This code requires graphics and presentation to use the same queue!\n");

  VkDevice                      device    = swapchain->device;
  const VkAllocationCallbacks * allocator = swapchain->allocator;

  VkSwapchainCreateInfoKHR const swapchain_info = {
    .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .flags                 = 0,
    .pNext                 = NULL,
    .surface               = swapchain->surface_khr,
    .minImageCount         = surface_image_count,
    .imageFormat           = swapchain->surface_format.format,
    .imageColorSpace       = swapchain->surface_format.colorSpace,
    .imageExtent           = swapchain->surface_extent,
    .imageUsage            = image_usage,
    .preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .imageArrayLayers      = 1,
    .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL,
    .presentMode           = swapchain->present_mode,
    .oldSwapchain          = VK_NULL_HANDLE,
    .clipped               = VK_FALSE,
  };
  vk(CreateSwapchainKHR(device, &swapchain_info, allocator, &swapchain->swapchain_khr));

  swapchain->image_count = 0;
  vk(GetSwapchainImagesKHR(device, swapchain->swapchain_khr, &swapchain->image_count, NULL));

  ASSERT_MSG(swapchain->image_count > 0, "Could not create swapchain images!\n");

  ASSERT_MSG(swapchain->image_count <= MAX_VK_SWAPCHAIN_IMAGES,
             "Too many swapchain images (%d should be <= %d)\n",
             swapchain->image_count,
             MAX_VK_SWAPCHAIN_IMAGES);

  vk(GetSwapchainImagesKHR(device,
                           swapchain->swapchain_khr,
                           &swapchain->image_count,
                           swapchain->images));

  for (uint32_t nn = 0; nn < swapchain->image_count; ++nn)
    {
      const VkImageViewCreateInfo image_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = NULL,
        .image = swapchain->images[nn],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = swapchain->surface_format.format,
        .components = {
          .r = VK_COMPONENT_SWIZZLE_R,
          .g = VK_COMPONENT_SWIZZLE_G,
          .b = VK_COMPONENT_SWIZZLE_B,
          .a = VK_COMPONENT_SWIZZLE_A,
        },
        .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = 0,
          .levelCount = 1,
          .baseArrayLayer = 0,
          .layerCount = 1,
        },
      };
      vk(CreateImageView(device, &image_view_info, allocator, &swapchain->image_views[nn]));
    }

  vkGetDeviceQueue(device,
                   config->present_queue_family,
                   config->present_queue_index,
                   &swapchain->present_queue);
  ASSERT_MSG(swapchain->present_queue != VK_NULL_HANDLE,
             "Could not get presentation queue handle!\n");

  vk(CreateCommandPool(device,
                       &(const VkCommandPoolCreateInfo){
                         .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                         .queueFamilyIndex = config->present_queue_family,
                         .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                       },
                       allocator,
                       &swapchain->present_command_pool));

  vk(AllocateCommandBuffers(device,
                            &(const VkCommandBufferAllocateInfo){
                              .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                              .commandPool        = swapchain->present_command_pool,
                              .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              .commandBufferCount = swapchain->image_count,
                            },
                            swapchain->image_present_command_buffers));

  for (uint32_t nn = 0; nn < swapchain->frame_count; ++nn)
    {
      swapchain->frame_available_semaphores[nn] = vk_swapchain_create_semaphore(swapchain);
      swapchain->frame_rendered_semaphores[nn]  = vk_swapchain_create_semaphore(swapchain);
      swapchain->frame_inflight_fences[nn]      = vk_swapchain_create_fence(swapchain, true);

#if DEBUG_SWAPCHAIN
      swapchain->frame_acquired_fences[nn] = vk_swapchain_create_fence(swapchain, false);
#endif
    }

  if (config->use_presentation_layout)
    {
      // Transition each swapchain image to presentation layout to considerably
      // simplify future usage.
      vk_swapchain_transition_image_layouts(swapchain,
                                            swapchain->present_queue,
                                            swapchain->present_command_pool,
                                            VK_IMAGE_LAYOUT_UNDEFINED,
                                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

  return swapchain;
}

VkSemaphore
vk_swapchain_get_image_acquired_semaphore(const vk_swapchain_t * swapchain)
{
  uint32_t frame_index = swapchain->frame_index;
  return swapchain->frame_available_semaphores[frame_index];
}

VkSemaphore
vk_swapchain_take_image_acquired_semaphore(vk_swapchain_t * swapchain)
{
  uint32_t    frame_index = swapchain->frame_index;
  VkSemaphore result      = swapchain->frame_available_semaphores[frame_index];
  swapchain->frame_available_semaphores[frame_index] = VK_NULL_HANDLE;
  return result;
}

VkSemaphore
vk_swapchain_get_image_rendered_semaphore(const vk_swapchain_t * swapchain)
{
  uint32_t frame_index = swapchain->frame_index;
  return swapchain->frame_rendered_semaphores[frame_index];
}

VkSemaphore
vk_swapchain_take_image_rendered_semaphore(vk_swapchain_t * swapchain)
{
  uint32_t    frame_index = swapchain->frame_index;
  VkSemaphore result      = swapchain->frame_rendered_semaphores[frame_index];
  swapchain->frame_rendered_semaphores[frame_index] = VK_NULL_HANDLE;
  return result;
}

uint32_t
vk_swapchain_get_image_index(const vk_swapchain_t * swapchain)
{
  return swapchain->image_index;
}

bool
vk_swapchain_acquire_next_image(vk_swapchain_t * swapchain, uint32_t * image_index)
{
  // Wait for the frame's fence to ensure we're not feeding too many of them
  // to the presentation engine.
  // BUG: The Fuchsia image pipe layer which provides vkAcquireNextImageKHR()
  // does not support a non-null fence argument, and will crash at runtime!
  uint32_t frame_index = swapchain->frame_index;
#if DEBUG_SWAPCHAIN && !defined(__Fuchsia__)
  VkFence acquired_fence = swapchain->frame_acquired_fences[frame_index];
#else
  const VkFence acquired_fence = VK_NULL_HANDLE;
#endif

  VkSemaphore semaphore = swapchain->frame_available_semaphores[frame_index];
  if (semaphore == VK_NULL_HANDLE)
    {
      semaphore                                          = vk_swapchain_create_semaphore(swapchain);
      swapchain->frame_available_semaphores[frame_index] = semaphore;
    }

  VkResult result = vkAcquireNextImageKHR(swapchain->device,
                                          swapchain->swapchain_khr,
                                          UINT64_MAX,
                                          semaphore,
                                          acquired_fence,
                                          image_index);
  switch (result)
    {
      case VK_SUCCESS:
      case VK_SUBOPTIMAL_KHR:
        break;
      case VK_ERROR_OUT_OF_DATE_KHR:
        *image_index = ~0U;
        return false;
      default:
        VK_CHECK_MSG(result, "Could not acquire next swapchain image");
    }

#if DEBUG_SWAPCHAIN && !defined(__Fuchsia__)
  const uint64_t one_millisecond_ns = 1000000ULL;  // 1ms in nanoseconds.
  const uint64_t timeout_ns         = 500 * one_millisecond_ns;
  result = vkWaitForFences(swapchain->device, 1, &acquired_fence, VK_TRUE, timeout_ns);
  if (result == VK_TIMEOUT)
    {
      ASSERT_MSG(result != VK_TIMEOUT, "Timeout while waiting for acquired fence!\n");
    }
  vkResetFences(swapchain->device, 1, &acquired_fence);
#endif  // DEBUG_SWAPCHAIN

  swapchain->image_index = *image_index;
  PRINT("#%2u: ACQUIRED image_index=%u signal_sem=%p\n",
        swapchain->image_number,
        *image_index,
        semaphore);

  return true;
}

bool
vk_swapchain_present_image(vk_swapchain_t * swapchain)
{
  uint32_t frame_index = swapchain->frame_index;

  const VkPresentInfoKHR presentInfo = {
    .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores    = &swapchain->frame_rendered_semaphores[frame_index],
    .swapchainCount     = 1,
    .pSwapchains        = &swapchain->swapchain_khr,
    .pImageIndices      = &swapchain->image_index,
  };

  VkResult result = vkQueuePresentKHR(swapchain->present_queue, &presentInfo);
  switch (result)
    {
      case VK_SUCCESS:
      case VK_SUBOPTIMAL_KHR:
        break;
      case VK_ERROR_OUT_OF_DATE_KHR:
        return false;
      default:
        VK_CHECK_MSG(result, "Problem during presentation!");
    }

  PRINT("#%2u: PRESENTED frame_index=%u image_index=%u wait_sem=%p\n",
        swapchain->image_counter,
        frame_index,
        swapchain->image_index,
        swapchain->frame_rendered_semaphores[frame_index]);
  //SLEEP(2);

  swapchain->frame_index = (frame_index + 1) % swapchain->frame_count;
  swapchain->image_counter += 1;
  return true;
}

void
vk_swapchain_destroy(vk_swapchain_t * swapchain)
{
  VkDevice                      device      = swapchain->device;
  const VkAllocationCallbacks * allocator   = swapchain->allocator;
  uint32_t                      image_count = swapchain->image_count;

  for (uint32_t nn = 0; nn < swapchain->frame_count; ++nn)
    {
#if DEBUG_SWAPCHAIN
      vkDestroyFence(device, swapchain->frame_acquired_fences[nn], allocator);
#endif
      vkDestroyFence(device, swapchain->frame_inflight_fences[nn], allocator);
      vkDestroySemaphore(device, swapchain->frame_available_semaphores[nn], allocator);
      vkDestroySemaphore(device, swapchain->frame_rendered_semaphores[nn], allocator);
    }
  swapchain->frame_count = 0;
  swapchain->frame_index = 0;

  if (swapchain->present_command_pool != VK_NULL_HANDLE)
    {
      vkFreeCommandBuffers(device,
                           swapchain->present_command_pool,
                           image_count,
                           swapchain->image_present_command_buffers);

      vkDestroyCommandPool(device, swapchain->present_command_pool, allocator);
      swapchain->present_command_pool = VK_NULL_HANDLE;
    }

  for (uint32_t nn = 0; nn < image_count; ++nn)
    {
      vkDestroyImageView(device, swapchain->image_views[nn], allocator);
      swapchain->image_views[nn] = VK_NULL_HANDLE;
      swapchain->images[nn]      = VK_NULL_HANDLE;
    }
  swapchain->image_count = 0;

  if (swapchain->swapchain_khr != VK_NULL_HANDLE)
    {
      vkDestroySwapchainKHR(device, swapchain->swapchain_khr, allocator);
      swapchain->swapchain_khr = VK_NULL_HANDLE;
    }
}

void
vk_swapchain_print(const vk_swapchain_t * swapchain)
{
  printf("  Swapchain state:\n");
  printf("    VkSurfaceKHR:       %p\n", swapchain->surface_khr);
  printf("    VkSwapchainKHR:     %p\n", swapchain->swapchain_khr);
  printf("    Present queue:      %p\n", swapchain->present_queue);
  printf("    Extent:             %dx%d\n",
         swapchain->surface_extent.width,
         swapchain->surface_extent.height);
  printf("    SurfaceFormat:      %s\n",
         vk_surface_format_khr_to_string(swapchain->surface_format));

  printf("    Image count:        %u\n", swapchain->image_count);
  for (uint32_t nn = 0; nn < swapchain->image_count; ++nn)
    {
      printf("      image #%u\n", nn);
      printf("        image:           %p\n", swapchain->images[nn]);
      printf("        image view:      %p\n", swapchain->image_views[nn]);
    }
  printf("    Frame count:        %u\n", swapchain->frame_count);
  for (uint32_t nn = 0; nn < swapchain->frame_count; ++nn)
    {
      printf("      frame #%u\n", nn);
      printf("        acquired_semaphore:   %p\n", swapchain->frame_available_semaphores[nn]);
      printf("        rendered_semaphore:   %p\n", swapchain->frame_rendered_semaphores[nn]);
    }

  {
    vk_device_surface_info_t surface_info;
    vk_device_surface_info_init(&surface_info,
                                swapchain->physical_device,
                                swapchain->surface_khr,
                                swapchain->instance);

    vk_device_surface_info_print(&surface_info);
    vk_device_surface_info_destroy(&surface_info);
  }
}
