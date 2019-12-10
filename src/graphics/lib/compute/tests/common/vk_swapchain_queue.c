// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_swapchain_queue.h"

#include <stdio.h>
#include <stdlib.h>

#include "tests/common/utils.h"
#include "tests/common/vk_swapchain.h"
#include "tests/common/vk_utils.h"

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

// Maximum number of swapchain images
#define MAX_VK_SWAPCHAIN_IMAGES 8

// The vk_swapchain_queue_t instance struct.
// |size| is the number of images in the swapchain.
// |index| is the index of the current image.
// |images| is an array of |size| |vk_swapchain_queue_image_t| instances.
struct vk_swapchain_queue
{
  uint32_t                   size;
  uint32_t                   index;
  uint32_t                   counter;
  vk_swapchain_queue_image_t images[MAX_VK_SWAPCHAIN_IMAGES];

  vk_swapchain_t *              swapchain;
  VkDevice                      device;
  const VkAllocationCallbacks * allocator;
  VkCommandPool                 command_pool;
  VkQueue                       command_queue;
};

static void
vk_swapchain_queue_init(vk_swapchain_queue_t * queue, const vk_swapchain_queue_config_t * config)
{
  vk_swapchain_t *              swapchain = config->swapchain;
  VkDevice                      device    = config->device;
  const VkAllocationCallbacks * allocator = config->allocator;

  ASSERT_MSG(swapchain, "vk_swapchain_queue_config_t::swapchain required!\n");
  ASSERT_MSG(device != VK_NULL_HANDLE, "vk_swapchain_queue_config_t::device required!\n");

  queue->swapchain = config->swapchain;
  queue->device    = device;
  queue->allocator = allocator;

  queue->size = vk_swapchain_get_image_count(config->swapchain);

  ASSERT_MSG(queue->size < MAX_VK_SWAPCHAIN_IMAGES,
             "Too many swapchain images %u, only %u supported!\n",
             queue->size,
             MAX_VK_SWAPCHAIN_IMAGES);

  ASSERT_MSG(config->sync_semaphores_count < MAX_VK_SYNC_SEMAPHORES,
             "Too many sync semaphores %u, should be <= %u\n",
             config->sync_semaphores_count,
             MAX_VK_SYNC_SEMAPHORES);

  const VkFenceCreateInfo fenceCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };

  vkGetDeviceQueue(device, config->queue_family, config->queue_index, &queue->command_queue);
  ASSERT_MSG(queue->command_queue != VK_NULL_HANDLE, "Could not get command queue handle!\n");

  vk(CreateCommandPool(device,
                       &(const VkCommandPoolCreateInfo){
                         .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                         .queueFamilyIndex = config->queue_family,
                         .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                       },
                       allocator,
                       &queue->command_pool));

  const VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool        = queue->command_pool,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1,
  };

  VkExtent2D extent = vk_swapchain_get_extent(queue->swapchain);

  for (uint32_t nn = 0; nn < queue->size; ++nn)
    {
      vk_swapchain_queue_image_t * image = &queue->images[nn];

      image->image      = vk_swapchain_get_image(swapchain, nn);
      image->image_view = vk_swapchain_get_image_view(swapchain, nn);

      vk(AllocateCommandBuffers(device, &commandBufferAllocateInfo, &image->command_buffer));

      vk(CreateFence(device, &fenceCreateInfo, allocator, &image->fence));

      image->framebuffer = VK_NULL_HANDLE;
      if (config->enable_framebuffers != VK_NULL_HANDLE)
        {
          const VkFramebufferCreateInfo framebufferInfo = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = config->enable_framebuffers,
            .attachmentCount = 1,
            .pAttachments    = &image->image_view,
            .width           = extent.width,
            .height          = extent.height,
            .layers          = 1,
          };
          vk(CreateFramebuffer(device, &framebufferInfo, allocator, &image->framebuffer));
        }

      for (uint32_t mm = 0; mm < MAX_VK_SYNC_SEMAPHORES; ++mm)
        {
          image->sync_semaphores[mm] = VK_NULL_HANDLE;
          if (mm < config->sync_semaphores_count)
            {
              vk(CreateSemaphore(device,
                                 &(const VkSemaphoreCreateInfo){
                                   .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                 },
                                 allocator,
                                 &image->sync_semaphores[mm]));
            }
        }
    }
}

vk_swapchain_queue_t *
vk_swapchain_queue_create(const vk_swapchain_queue_config_t * config)
{
  vk_swapchain_queue_t * queue = calloc(1, sizeof(*queue));
  vk_swapchain_queue_init(queue, config);
  return queue;
}

uint32_t
vk_swapchain_queue_get_size(const vk_swapchain_queue_t * queue)
{
  return queue->size;
}

uint32_t
vk_swapchain_queue_get_index(const vk_swapchain_queue_t * queue)
{
  return queue->index;
}

const vk_swapchain_queue_image_t *
vk_swapchain_queue_get_image(const vk_swapchain_queue_t * queue, uint32_t image_index)
{
  ASSERT_MSG(image_index < queue->size, "Invalid image index %u\n", image_index);
  return &queue->images[image_index];
}

extern const vk_swapchain_queue_image_t *
vk_swapchain_queue_acquire_next_image(vk_swapchain_queue_t * queue)
{
  uint32_t image_number = queue->counter + 1;
  UNUSED(image_number);

  PRINT("#%2u: ACQUIRING SWAPCHAIN IMAGE\n", image_number);
  if (!vk_swapchain_acquire_next_image(queue->swapchain, &queue->index))
    return NULL;

  // Wait for the frame's fence to ensure we're not feeding too many of them
  // to the presentation engine.
  const vk_swapchain_queue_image_t * image = &queue->images[queue->index];

  PRINT("#%2u: WAITING fence[%u]=%p\n", image_number, queue->index, image->fence);

  const uint64_t one_millisecond_ns = 1000000ULL;  // 1ms in nanoseconds.
  uint64_t       timeout_ns         = 500 * one_millisecond_ns;
  VkDevice       device             = queue->device;
  VkResult       result = vkWaitForFences(device, 1, &image->fence, VK_TRUE, timeout_ns);
  if (result == VK_TIMEOUT)
    {
      ASSERT_MSG(result != VK_TIMEOUT, "Timeout while waiting for fence!\n");
    }
  vkResetFences(device, 1, &image->fence);

  PRINT("#%2u: WAITED\n", image_number);
  return image;
}

extern void
vk_swapchain_queue_submit_and_present_image(vk_swapchain_queue_t * queue)
{
  vk_swapchain_queue_submit_and_present_image_wait_one(
    queue,
    vk_swapchain_get_image_acquired_semaphore(queue->swapchain),
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

extern void
vk_swapchain_queue_submit_and_present_image_wait_one(vk_swapchain_queue_t * queue,
                                                     VkSemaphore            wait_semaphore,
                                                     VkPipelineStageFlags   wait_stages)
{
  uint32_t image_number = queue->counter + 1;
  UNUSED(image_number);

  const vk_swapchain_queue_image_t * image = &queue->images[queue->index];
  VkSemaphore signal_semaphore = vk_swapchain_get_image_rendered_semaphore(queue->swapchain);

  PRINT("#%2u: SUBMITTING image_index=%u wait_sem=%p signal_sem=%p fence=%p\n",
        image_number,
        queue->index,
        wait_semaphore,
        signal_semaphores,
        image->fence);

  //printf("SUBMIT(f%u,i%u)!", current_frame, image_index); fflush(stdout);
  vk_submit_one(wait_semaphore,
                wait_stages,
                signal_semaphore,
                queue->command_queue,
                image->command_buffer,
                image->fence);

  PRINT("#%2u: SUBMITTED cmd_buffer=%p\n", image_number, image->command_buffer);

  vk_swapchain_present_image(queue->swapchain);

  queue->counter += 1;
}

extern void
vk_swapchain_queue_destroy(vk_swapchain_queue_t * queue)
{
  VkDevice                      device    = queue->device;
  const VkAllocationCallbacks * allocator = queue->allocator;
  for (uint32_t nn = 0; nn < queue->size; ++nn)
    {
      vk_swapchain_queue_image_t * image = &queue->images[nn];

      for (uint32_t mm = 0; mm < MAX_VK_SYNC_SEMAPHORES; ++mm)
        {
          if (image->sync_semaphores[mm] != VK_NULL_HANDLE)
            {
              vkDestroySemaphore(device, image->sync_semaphores[mm], allocator);
              image->sync_semaphores[mm] = VK_NULL_HANDLE;
            }
        }

      if (image->framebuffer != VK_NULL_HANDLE)
        {
          vkDestroyFramebuffer(device, image->framebuffer, allocator);
          image->framebuffer = VK_NULL_HANDLE;
        }

      vkDestroyFence(device, image->fence, allocator);
      image->fence = VK_NULL_HANDLE;

      vkFreeCommandBuffers(device, queue->command_pool, 1, &image->command_buffer);
      image->command_buffer = VK_NULL_HANDLE;
    }

  vkDestroyCommandPool(device, queue->command_pool, allocator);
  queue->command_pool  = VK_NULL_HANDLE;
  queue->command_queue = VK_NULL_HANDLE;
}
