// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_swapchain.h"

#include <stdio.h>
#include <stdlib.h>

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
// Surface presentation info
//

typedef struct
{
  uint32_t present_modes_count;
  uint32_t formats_count;

  VkSurfaceCapabilitiesKHR capabilities;
  VkPresentModeKHR *       present_modes;
  VkSurfaceFormatKHR *     formats;
} SurfaceInfo;

static SurfaceInfo
surface_info_create(VkPhysicalDevice physical_device, VkSurfaceKHR surface, VkInstance instance)
{
  SurfaceInfo info = {};

  // Get capabilities.
  vk(GetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &info.capabilities));

  // Get formats.
  vk(GetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &info.formats_count, NULL));
  info.formats = calloc(info.formats_count, sizeof(info.formats[0]));
  vk(GetPhysicalDeviceSurfaceFormatsKHR(physical_device,
                                        surface,
                                        &info.formats_count,
                                        info.formats));

  // Get present modes.
  vk(GetPhysicalDeviceSurfacePresentModesKHR(physical_device,
                                             surface,
                                             &info.present_modes_count,
                                             NULL));
  info.present_modes = calloc(info.present_modes_count, sizeof(info.present_modes[0]));
  vk(GetPhysicalDeviceSurfacePresentModesKHR(physical_device,
                                             surface,
                                             &info.present_modes_count,
                                             info.present_modes));
  return info;
}

static void
surface_info_destroy(SurfaceInfo * info)
{
  free(info->formats);
  info->formats       = NULL;
  info->formats_count = 0;

  free(info->present_modes);
  info->present_modes       = NULL;
  info->present_modes_count = 0;
}

static const char *
present_mode_to_string(VkPresentModeKHR arg)
{
  static char temp[64];
#define CASE(arg)                                                                                  \
  case VK_PRESENT_MODE_##arg:                                                                      \
    return "VK_PRESENT_MODE_" #arg
  switch (arg)
    {
      CASE(IMMEDIATE_KHR);
      CASE(MAILBOX_KHR);
      CASE(FIFO_KHR);
      CASE(FIFO_RELAXED_KHR);
#undef CASE
      default:
        snprintf(temp, sizeof(temp), "UNKNOWN(%u)", (unsigned)arg);
        return temp;
    }
}

static const char *
format_to_string(const VkFormat arg)
{
  static char temp[64];
#define CASE(arg)                                                                                  \
  case VK_FORMAT_##arg:                                                                            \
    return "VK_FORMAT_" #arg
  switch (arg)
    {
      CASE(UNDEFINED);
      CASE(B8G8R8A8_UNORM);
      CASE(B8G8R8A8_SRGB);
#undef CASE
      default:
        snprintf(temp, sizeof(temp), "UNKNOWN(%u)", (unsigned)arg);
        return temp;
    }
}

static const char *
colorspace_to_string(const VkColorSpaceKHR arg)
{
  static char temp[64];
#define CASE(arg)                                                                                  \
  case VK_COLOR_SPACE_##arg:                                                                       \
    return "VK_COLOR_SPACE_" #arg
  switch (arg)
    {
      CASE(SRGB_NONLINEAR_KHR);
#undef CASE
      default:
        snprintf(temp, sizeof(temp), "UNKNOWN(%u)", (unsigned)arg);
        return temp;
    }
}

static void
surface_info_print(const SurfaceInfo * info)
{
  printf("Surface info: num_present_modes=%u num_formats=%u\n",
         info->present_modes_count,
         info->formats_count);

  // Print capabilities.
  {
    const VkSurfaceCapabilitiesKHR * caps = &info->capabilities;
    printf("  minImageCount:             %u\n", caps->minImageCount);
    printf("  maxImageCount:             %u\n", caps->maxImageCount);
    printf("  currentExtent:             %ux%u\n",
           caps->currentExtent.width,
           caps->currentExtent.height);
    printf("  minImageExtent:            %ux%u\n",
           caps->minImageExtent.width,
           caps->minImageExtent.height);
    printf("  maxImageExtent:            %ux%u\n",
           caps->maxImageExtent.width,
           caps->maxImageExtent.height);
    printf("  maxImageArrayLayers:       %u\n", caps->maxImageArrayLayers);
  }

  for (uint32_t nn = 0; nn < info->present_modes_count; ++nn)
    {
      printf("     %s\n", present_mode_to_string(info->present_modes[nn]));
    }

  for (uint32_t nn = 0; nn < info->formats_count; ++nn)
    {
      const VkSurfaceFormatKHR sf = info->formats[nn];
      printf("     %s : %s\n", format_to_string(sf.format), colorspace_to_string(sf.colorSpace));
    }
}

//
//
//

// Maximum number of swapchain images
#define MAX_VK_SWAPCHAIN_IMAGES 8

struct vk_swapchain_t
{
  VkInstance                    instance;
  VkDevice                      device;
  const VkAllocationCallbacks * allocator;

  VkPhysicalDevice   physical_device;
  VkSurfaceKHR       surface_khr;
  VkSwapchainKHR     swapchain_khr;
  uint32_t           graphics_queue_family;
  VkQueue            graphics_queue;
  VkQueue            present_queue;
  VkSurfaceFormatKHR surface_format;
  VkExtent2D         surface_extent;
  VkPresentModeKHR   present_mode;
  VkCommandPool      graphics_command_pool;
  VkCommandPool      present_command_pool;

  // The following arrays are indexed by swapchain image index.
  uint32_t        image_count;
  uint32_t        image_index;
  uint32_t        image_counter;
  bool            has_framebuffers;
  VkImage         images[MAX_VK_SWAPCHAIN_IMAGES];
  VkImageView     image_views[MAX_VK_SWAPCHAIN_IMAGES];
  VkCommandBuffer image_graphics_command_buffers[MAX_VK_SWAPCHAIN_IMAGES];
  VkCommandBuffer image_present_command_buffers[MAX_VK_SWAPCHAIN_IMAGES];
  VkFramebuffer   image_framebuffers[MAX_VK_SWAPCHAIN_IMAGES];

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

vk_swapchain_surface_info_t
vk_swapchain_get_surface_info(const vk_swapchain_t * swapchain)
{
  vk_swapchain_surface_info_t info = {
    .surface_extent = swapchain->surface_extent,
    .surface_format = swapchain->surface_format,
    .image_count    = swapchain->image_count,
    .frame_count    = swapchain->frame_count,
  };
  return info;
}

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

vk_swapchain_image_data_t
vk_swapchain_get_image_data(const vk_swapchain_t * swapchain, uint32_t image_index)
{
  ASSERT_MSG(image_index < swapchain->image_count, "Invalid image index: %u\n", image_index);
  vk_swapchain_image_data_t data = {
    .image          = swapchain->images[image_index],
    .image_view     = swapchain->image_views[image_index],
    .command_buffer = swapchain->image_graphics_command_buffers[image_index],
    .framebuffer    = swapchain->image_framebuffers[image_index],
  };
  return data;
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

VkCommandBuffer
vk_swapchain_get_image_command_buffer(const vk_swapchain_t * swapchain, uint32_t image_index)
{
  ASSERT_MSG(image_index < swapchain->image_count, "Invalid image index: %u\n", image_index);
  return swapchain->image_graphics_command_buffers[image_index];
}

VkFramebuffer
vk_swapchain_get_image_framebuffer(const vk_swapchain_t * swapchain, uint32_t image_index)
{
  ASSERT_MSG(image_index < swapchain->image_count, "Invalid image index: %u\n", image_index);
  ASSERT_MSG(swapchain->has_framebuffers,
             "vk_swapchain_enable_image_framebuffers() was not called!\n");
  return swapchain->image_framebuffers[image_index];
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
  SurfaceInfo surface_info =
    surface_info_create(config->physical_device, config->surface_khr, config->instance);

  VkFormat wanted_format = config->pixel_format;
  if (wanted_format == VK_FORMAT_UNDEFINED)
    {
      wanted_format = VK_FORMAT_B8G8R8A8_UNORM;
    }

  if (surface_info.formats_count >= 1 && surface_info.formats[0].format == VK_FORMAT_UNDEFINED)
    {
      // The surface has not favorite display format.
      swapchain->surface_format = (VkSurfaceFormatKHR){
        .format     = wanted_format,
        .colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
      };
    }
  else
    {
      // Fallback to the first one.
      if (!swapchain->surface_format.format)
        {
          ASSERT_MSG(surface_info.formats_count >= 1,
                     "This surface does not provide any presentation format!\n");

          // TODO(digit): Allow the client to select a favorite format if available.
          swapchain->surface_format = surface_info.formats[0];
        }
    }

  // TODO(digit): Allow the client to specify an alternative presentation mode.
  swapchain->present_mode = VK_PRESENT_MODE_FIFO_KHR;

  uint32_t surface_image_count = surface_info.capabilities.minImageCount;

  // NOTE: BUG: AEMU reports an maxImageCount of 0 which is wrong!!
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

  surface_info_destroy(&surface_info);

  // TODO: Support graphics_queue_family != present_queue_family
  swapchain->graphics_queue_family = config->graphics_queue_family;
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
    .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
      const VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      };

      const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
      };

      vk(CreateSemaphore(device, &sem_info, allocator, &swapchain->frame_available_semaphores[nn]));
      vk(CreateSemaphore(device, &sem_info, allocator, &swapchain->frame_rendered_semaphores[nn]));
      vk(CreateFence(device, &fence_info, allocator, &swapchain->frame_inflight_fences[nn]));

#if DEBUG_SWAPCHAIN
      const VkFenceCreateInfo unsignaled_fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      };

      vk(CreateFence(device,
                     &unsignaled_fence_info,
                     allocator,
                     &swapchain->frame_acquired_fences[nn]));
#endif
    }

  return swapchain;
}

VkFence
vk_swapchain_get_image_rendered_fence(const vk_swapchain_t * swapchain)
{
  uint32_t frame_index = swapchain->frame_index;
  return swapchain->frame_inflight_fences[frame_index];
}

VkSemaphore
vk_swapchain_get_image_acquired_semaphore(const vk_swapchain_t * swapchain)
{
  uint32_t frame_index = swapchain->frame_index;
  return swapchain->frame_available_semaphores[frame_index];
}

VkSemaphore
vk_swapchain_get_image_rendered_semaphore(const vk_swapchain_t * swapchain)
{
  uint32_t frame_index = swapchain->frame_index;
  return swapchain->frame_rendered_semaphores[frame_index];
}

bool
vk_swapchain_prepare_next_image(vk_swapchain_t * swapchain, uint32_t * image_index)
{
  // Wait for the frame's fence to ensure we're not feeding too many of them
  // to the presentation engine.
  uint32_t frame_index    = swapchain->frame_index;
  VkFence  inflight_fence = swapchain->frame_inflight_fences[frame_index];
  // BUG: The Fuchsia image pipe layer which provides vkAcquireNextImageKHR()
  // does not support a non-null fence argument, and will crash at runtime!
#if DEBUG_SWAPCHAIN && !defined(__Fuchsia__)
  VkFence acquired_fence = swapchain->frame_acquired_fences[frame_index];
#else
  const VkFence acquired_fence = VK_NULL_HANDLE;
#endif

  uint32_t image_number = ++swapchain->image_counter;
  UNUSED(image_number);

  PRINT("#%2u: WAITING fence=%p frame_index=%u\n", image_number, inflight_fence, frame_index);

  const uint64_t one_millisecond_ns = 1000000ULL;  // 1ms in nanoseconds.
  uint64_t       timeout_ns         = 500 * one_millisecond_ns;
  VkDevice       device             = swapchain->device;
  VkResult       result = vkWaitForFences(device, 1, &inflight_fence, VK_TRUE, timeout_ns);
  if (result == VK_TIMEOUT)
    {
      //printf("SWAPCHAIN TIMEOUT (waiting 10s before aborting)! ");
      //fflush(stdout);
      //sleep(10);
      ASSERT_MSG(result != VK_TIMEOUT, "Timeout while waiting for fence!\n");
    }
  vkResetFences(device, 1, &inflight_fence);

  PRINT("#%2u: WAITED\n", image_number);

  result = vkAcquireNextImageKHR(device,
                                 swapchain->swapchain_khr,
                                 UINT64_MAX,
                                 swapchain->frame_available_semaphores[frame_index],
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
  result = vkWaitForFences(device, 1, &acquired_fence, VK_TRUE, timeout_ns);
  if (result == VK_TIMEOUT)
    {
      ASSERT_MSG(result != VK_TIMEOUT, "Timeout while waiting for acquired fence!\n");
    }
  vkResetFences(device, 1, &acquired_fence);
#endif  // DEBUG_SWAPCHAIN

  swapchain->image_index = *image_index;
  PRINT("#%2u: ACQUIRED image_index=%u signal_sem=%p\n",
        image_number,
        *image_index,
        swapchain->frame_available_semaphores[frame_index]);

  return true;
}

void
vk_swapchain_submit_image(vk_swapchain_t * swapchain)
{
  ASSERT_MSG(swapchain->graphics_queue != VK_NULL_HANDLE,
             "vk_swapchain_enable_image_command_buffers() was not called!\n");

  uint32_t frame_index = swapchain->frame_index;

  // Wait on a single semaphore for now.
  const VkPipelineStageFlags waitStages[] = {
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  };

  uint32_t image_number = swapchain->image_counter;
  UNUSED(image_number);

  PRINT("#%2u: SUBMITTING frame_index=%u image_index=%u wait_sem=%p signal_sem=%p fence=%p\n",
        image_number,
        frame_index,
        swapchain->image_index,
        swapchain->frame_available_semaphores[frame_index],
        swapchain->frame_rendered_semaphores[frame_index],
        swapchain->frame_inflight_fences[frame_index]);

  const VkSubmitInfo submitInfo = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount   = 1,
    .pWaitSemaphores      = &swapchain->frame_available_semaphores[frame_index],
    .pWaitDstStageMask    = waitStages,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &swapchain->image_graphics_command_buffers[swapchain->image_index],
    .signalSemaphoreCount = 1,
    .pSignalSemaphores    = &swapchain->frame_rendered_semaphores[frame_index],
  };

  //printf("SUBMIT(f%u,i%u)!", current_frame, image_index); fflush(stdout);
  vk(QueueSubmit(swapchain->graphics_queue,
                 1,
                 &submitInfo,
                 swapchain->frame_inflight_fences[frame_index]));

  PRINT("#%2u: SUBMITTED cmd_buffer=%p\n", image_number, command_buffers[0]);
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
  return true;
}

void
vk_swapchain_enable_image_command_buffers(vk_swapchain_t * swapchain,
                                          uint32_t         graphics_queue_family,
                                          uint32_t         graphics_queue_index)
{
  if (swapchain->graphics_command_pool != VK_NULL_HANDLE)
    return;

  vkGetDeviceQueue(swapchain->device,
                   graphics_queue_family,
                   graphics_queue_index,
                   &swapchain->graphics_queue);
  ASSERT_MSG(swapchain->graphics_queue != VK_NULL_HANDLE, "Could not get graphics queue handle!\n");

  vk(CreateCommandPool(swapchain->device,
                       &(const VkCommandPoolCreateInfo){
                         .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                         .queueFamilyIndex = graphics_queue_family,
                         .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                       },
                       swapchain->allocator,
                       &swapchain->graphics_command_pool));

  vk(AllocateCommandBuffers(swapchain->device,
                            &(const VkCommandBufferAllocateInfo){
                              .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                              .commandPool        = swapchain->graphics_command_pool,
                              .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              .commandBufferCount = swapchain->image_count,
                            },
                            swapchain->image_graphics_command_buffers));
}

extern VkQueue
vk_swapchain_get_graphics_queue(const vk_swapchain_t * swapchain)
{
  return swapchain->graphics_queue;
}

void
vk_swapchain_enable_image_framebuffers(vk_swapchain_t * swapchain, VkRenderPass render_pass)
{
  ASSERT_MSG(!swapchain->has_framebuffers,
             "vk_swapchain_enable_image_framebuffers() should not be called twice!\n");
  swapchain->has_framebuffers = true;
  for (uint32_t nn = 0; nn < swapchain->image_count; ++nn)
    {
      const VkFramebufferCreateInfo framebufferInfo = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = render_pass,
        .attachmentCount = 1,
        .pAttachments    = &swapchain->image_views[nn],
        .width           = swapchain->surface_extent.width,
        .height          = swapchain->surface_extent.height,
        .layers          = 1,
      };
      vk(CreateFramebuffer(swapchain->device,
                           &framebufferInfo,
                           swapchain->allocator,
                           &swapchain->image_framebuffers[nn]));
    }
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

  if (swapchain->graphics_command_pool != VK_NULL_HANDLE)
    {
      vkFreeCommandBuffers(device,
                           swapchain->graphics_command_pool,
                           image_count,
                           swapchain->image_graphics_command_buffers);

      vkDestroyCommandPool(device, swapchain->graphics_command_pool, allocator);
      swapchain->graphics_command_pool = VK_NULL_HANDLE;
    }

  if (swapchain->has_framebuffers)
    {
      for (uint32_t nn = 0; nn < image_count; ++nn)
        {
          vkDestroyFramebuffer(device, swapchain->image_framebuffers[nn], allocator);
        }
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
  printf("    Graphics queue:     %p\n", swapchain->graphics_queue);
  printf("    Present queue:      %p\n", swapchain->present_queue);
  printf("    Extent:             %dx%d\n",
         swapchain->surface_extent.width,
         swapchain->surface_extent.height);
  printf("    SurfaceFormat:      %s : %s\n",
         format_to_string(swapchain->surface_format.format),
         colorspace_to_string(swapchain->surface_format.colorSpace));

  printf("    Image count:        %u\n", swapchain->image_count);
  for (uint32_t nn = 0; nn < swapchain->image_count; ++nn)
    {
      printf("      image #%u\n", nn);
      printf("        image:           %p\n", swapchain->images[nn]);
      printf("        image view:      %p\n", swapchain->image_views[nn]);
      printf("        command_buffer:  %p\n", swapchain->image_graphics_command_buffers[nn]);
      printf("        framebuffers:    %p\n", swapchain->image_framebuffers[nn]);
    }
  printf("    Frame count:        %u\n", swapchain->frame_count);
  for (uint32_t nn = 0; nn < swapchain->frame_count; ++nn)
    {
      printf("      frame #%u\n", nn);
      printf("        acquired_semaphore:   %p\n", swapchain->frame_available_semaphores[nn]);
      printf("        rendered_semaphore:   %p\n", swapchain->frame_rendered_semaphores[nn]);
      printf("        inflight_fence:       %p\n", swapchain->frame_inflight_fences[nn]);
    }

  {
    SurfaceInfo surface_info =
      surface_info_create(swapchain->physical_device, swapchain->surface_khr, swapchain->instance);

    surface_info_print(&surface_info);
    surface_info_destroy(&surface_info);
  }
}
