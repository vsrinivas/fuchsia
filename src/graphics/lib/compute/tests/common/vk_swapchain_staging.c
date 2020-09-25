// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/vk_swapchain_staging.h"

#include <stdlib.h>

#include "tests/common/utils.h"
#include "tests/common/vk_image.h"
#include "tests/common/vk_image_utils.h"
#include "tests/common/vk_strings.h"
#include "tests/common/vk_utils.h"

#define MAX_IMAGES 8
#define MAX_FRAMES 3

//
//
//

static bool
is_format_rgba(VkFormat format)
{
  return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
}

static bool
is_format_bgra(VkFormat format)
{
  return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

// Return true iff |format1| and |format2| have swapped R and B channels.
static bool
formats_have_swapped_r_and_b(VkFormat format1, VkFormat format2)
{
  return (is_format_rgba(format1) && is_format_bgra(format2)) ||
         (is_format_bgra(format1) && is_format_rgba(format2));
}

//
//
//

typedef struct
{
  VkImage         swapchain_image;
  vk_image_t      target_image;
  VkCommandBuffer cmd_buffer;
} Stage;

struct vk_swapchain_staging
{
  uint32_t                      image_count;
  uint32_t                      frame_count;
  VkExtent2D                    extent;
  VkFormat                      target_format;
  VkDevice                      device;
  VkPhysicalDevice              physical_device;
  VkQueue                       present_queue;
  VkCommandPool                 command_pool;
  const VkAllocationCallbacks * allocator;
  Stage                         stages[MAX_IMAGES];
  VkSemaphore                   copy_semaphores[MAX_FRAMES];
};

vk_swapchain_staging_t *
vk_swapchain_staging_create(uint32_t                      image_count,
                            uint32_t                      frame_count,
                            VkImageUsageFlags             wanted_usage,
                            VkFormat                      wanted_format,
                            VkExtent2D                    swapchain_extent,
                            VkFormat                      swapchain_format,
                            const VkImage *               swapchain_images,
                            VkDevice                      device,
                            VkPhysicalDevice              physical_device,
                            uint32_t                      present_queue_family,
                            uint32_t                      present_queue_index,
                            const VkAllocationCallbacks * allocator)
{
  vk_swapchain_staging_t * staging = malloc(sizeof(*staging));

  ASSERT_MSG(image_count <= MAX_IMAGES,
             "Please increment MAX_IMAGES in this file to %d (currently %d)\n",
             image_count,
             MAX_IMAGES);

  ASSERT_MSG(frame_count <= MAX_FRAMES,
             "Please increment MAX_FRAMES in this file to %d (currently %d)\n",
             frame_count,
             MAX_FRAMES);

  *staging = (vk_swapchain_staging_t){
    .image_count     = image_count,
    .frame_count     = frame_count,
    .extent          = swapchain_extent,
    .target_format   = wanted_format,
    .device          = device,
    .physical_device = physical_device,
    .allocator       = allocator,
  };

  vkGetDeviceQueue(device, present_queue_family, present_queue_index, &staging->present_queue);

  vk(CreateCommandPool(device,
                       &(const VkCommandPoolCreateInfo){
                         .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                         .queueFamilyIndex = present_queue_family,
                         .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                       },
                       allocator,
                       &staging->command_pool));

  VkCommandBuffer cmd_buffers[MAX_IMAGES];
  vk(AllocateCommandBuffers(device,
                            &(const VkCommandBufferAllocateInfo){
                              .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                              .commandPool        = staging->command_pool,
                              .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              .commandBufferCount = image_count,
                            },
                            cmd_buffers));

  for (uint32_t nn = 0; nn < image_count; ++nn)
    {
      Stage * stage = &staging->stages[nn];

      stage->swapchain_image = swapchain_images[nn];
      stage->cmd_buffer      = cmd_buffers[nn];

      vk_image_alloc_device_local(&stage->target_image,
                                  wanted_format,
                                  swapchain_extent,
                                  wanted_usage,
                                  physical_device,
                                  device,
                                  allocator);

      if (formats_have_swapped_r_and_b(wanted_format, swapchain_format))
        {
          // Replace the ImageView in each target image with a different one
          // that changes the location of the R and B channels!
          vkDestroyImageView(device, stage->target_image.image_view, allocator);

          vk(CreateImageView(
              device,
              &(const VkImageViewCreateInfo){
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = stage->target_image.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = wanted_format,
                .components = {
                  .r = VK_COMPONENT_SWIZZLE_B,
                  .g = VK_COMPONENT_SWIZZLE_G,
                  .b = VK_COMPONENT_SWIZZLE_R,
                  .a = VK_COMPONENT_SWIZZLE_A,
                },
                .subresourceRange = {
                  .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                  .baseMipLevel   = 0,
                  .levelCount     = 1,
                  .baseArrayLayer = 0,
                  .layerCount     = 1,
                },
              },
              allocator,
              &stage->target_image.image_view));
        }
    }

  for (uint32_t nn = 0; nn < frame_count; ++nn)
    {
      vk(CreateSemaphore(device,
                         &(const VkSemaphoreCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                         },
                         allocator,
                         &staging->copy_semaphores[nn]));
    }

  // Transition target and temp images to presentation-src and general layouts.
  {
    VkCommandBuffer cmd_buffer = staging->stages[0].cmd_buffer;

    vk(BeginCommandBuffer(cmd_buffer,
                          &(const VkCommandBufferBeginInfo){
                            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                          }));

    for (uint32_t nn = 0; nn < image_count; ++nn)
      {
        Stage * stage = &staging->stages[nn];

        vk_cmd_image_layout_transition(cmd_buffer,
                                       stage->target_image.image,
                                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                       VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
      }

    vk(EndCommandBuffer(cmd_buffer));

    VkFence fence;
    vk(CreateFence(device,
                   &(const VkFenceCreateInfo){
                     .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                   },
                   allocator,
                   &fence));

    vk(QueueSubmit(staging->present_queue,
                   1,
                   &(const VkSubmitInfo){
                     .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                     .commandBufferCount = 1,
                     .pCommandBuffers    = &cmd_buffer,
                   },
                   fence));
    ;

    vk(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(device, fence, allocator);

    // Now record the commands that will be used on each presentation step.
    uint32_t width  = staging->extent.width;
    uint32_t height = staging->extent.height;

    for (uint32_t nn = 0; nn < image_count; ++nn)
      {
        Stage *         stage      = &staging->stages[nn];
        VkCommandBuffer cmd_buffer = stage->cmd_buffer;

        vk(BeginCommandBuffer(cmd_buffer,
                              &(const VkCommandBufferBeginInfo){
                                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                              }));

        // Transition swapchain image to transfer-dst-optimal.
        vk_cmd_image_layout_transition(cmd_buffer,
                                       stage->swapchain_image,
                                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Transition target image to transfer-src-optimal
        vk_cmd_image_layout_transition(cmd_buffer,
                                       stage->target_image.image,
                                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // Copy target image to swapchain image.
        vkCmdCopyImage(cmd_buffer,
                       stage->target_image.image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       stage->swapchain_image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1,
                       &(const VkImageCopy){
                         .srcSubresource = {
                           .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .layerCount = 1,
                         },
                         .dstSubresource = {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .layerCount = 1,
                         },
                         .extent = {
                           .width  = width,
                           .height = height,
                           .depth  = 1,
                         },
                       });

        // Transition target image back to present-src
        vk_cmd_image_layout_transition(cmd_buffer,
                                       stage->target_image.image,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        // Transition swapchain image back to present-src
        vk_cmd_image_layout_transition(cmd_buffer,
                                       stage->swapchain_image,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        vk(EndCommandBuffer(cmd_buffer));
      }
  }

  return staging;
}

void
vk_swapchain_staging_destroy(vk_swapchain_staging_t * staging)
{
  if (staging)
    {
      VkDevice                      device      = staging->device;
      const VkAllocationCallbacks * allocator   = staging->allocator;
      uint32_t                      image_count = staging->image_count;
      VkCommandBuffer               cmd_buffers[MAX_IMAGES];

      for (uint32_t nn = 0; nn < staging->frame_count; ++nn)
        vkDestroySemaphore(device, staging->copy_semaphores[nn], allocator);

      for (uint32_t nn = 0; nn < image_count; ++nn)
        {
          Stage * stage = &staging->stages[nn];

          cmd_buffers[nn] = stage->cmd_buffer;

          vk_image_free(&stage->target_image);

          stage->swapchain_image = VK_NULL_HANDLE;
          stage->cmd_buffer      = VK_NULL_HANDLE;
        }

      vkFreeCommandBuffers(device, staging->command_pool, image_count, cmd_buffers);
      vkDestroyCommandPool(device, staging->command_pool, allocator);

      staging->present_queue = VK_NULL_HANDLE;
      staging->allocator     = NULL;
      staging->device        = VK_NULL_HANDLE;
      staging->extent        = (VkExtent2D){};
      staging->frame_count   = 0;
      staging->image_count   = 0;
      free(staging);
    }
}

VkImage
vk_swapchain_staging_get_image(const vk_swapchain_staging_t * staging, uint32_t image_index)
{
  ASSERT_MSG(image_index < staging->image_count,
             "Invalid image index %d (should be < %d)\n",
             image_index,
             staging->image_count);

  return staging->stages[image_index].target_image.image;
}

VkImageView
vk_swapchain_staging_get_image_view(const vk_swapchain_staging_t * staging, uint32_t image_index)
{
  ASSERT_MSG(image_index < staging->image_count,
             "Invalid image index %d (should be < %d)\n",
             image_index,
             staging->image_count);

  return staging->stages[image_index].target_image.image_view;
}

VkSurfaceFormatKHR
vk_swapchain_staging_get_format(const vk_swapchain_staging_t * staging)
{
  return (VkSurfaceFormatKHR){
    .format     = staging->target_format,
    .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
  };
}

VkSemaphore
vk_swapchain_staging_present_image(vk_swapchain_staging_t * staging,
                                   uint32_t                 image_index,
                                   uint32_t                 frame_index,
                                   VkSemaphore              wait_semaphore)
{
  ASSERT_MSG(image_index < staging->image_count,
             "Invalid image index %d (should be < %d)\n",
             image_index,
             staging->image_count);

  ASSERT_MSG(frame_index < staging->frame_count,
             "Invalid frame index %d (should be < %d)\n",
             frame_index,
             staging->frame_count);

  // Transition target image to transfer-src, and blit image to tranfer-dst.
  Stage * stage = &staging->stages[image_index];

  VkSemaphore signal_semaphore = staging->copy_semaphores[frame_index];

  VkPipelineStageFlags wait_stages =
    VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

  vk(QueueSubmit(staging->present_queue,
                 1,
                 &(const VkSubmitInfo){
                   .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                   .waitSemaphoreCount   = 1,
                   .pWaitSemaphores      = &wait_semaphore,
                   .pWaitDstStageMask    = &wait_stages,
                   .commandBufferCount   = 1,
                   .pCommandBuffers      = &stage->cmd_buffer,
                   .signalSemaphoreCount = 1,
                   .pSignalSemaphores    = &signal_semaphore,
                 },
                 VK_NULL_HANDLE));

  return signal_semaphore;
}
