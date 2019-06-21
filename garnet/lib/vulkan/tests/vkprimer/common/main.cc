// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>
#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

#include "utils.h"
#include "vulkan_command_buffers.h"
#include "vulkan_command_pool.h"
#include "vulkan_framebuffer.h"
#include "vulkan_graphics_pipeline.h"
#include "vulkan_instance.h"
#include "vulkan_layer.h"
#include "vulkan_logical_device.h"
#include "vulkan_physical_device.h"
#include "vulkan_render_pass.h"
#include "vulkan_surface.h"
#include "vulkan_swapchain.h"
#include "vulkan_sync.h"

#if USE_GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

bool DrawFrame(const VulkanLogicalDevice& logical_device,
               const VulkanSync& sync, const VulkanSwapchain& swap_chain,
               const VulkanCommandBuffers& command_buffers);

void glfwErrorCallback(int error, const char* description) {
  fprintf(stderr, "glfwErrorCallback: %d : %s\n", error, description);
}

int main() {
  // INSTANCE
  const bool kEnableValidation = true;
  auto vulkan_instance = std::make_shared<VulkanInstance>();
#if USE_GLFW
  glfwInit();
  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwVulkanSupported()) {
    RTN_MSG(1, "glfwVulkanSupported has returned false.\n");
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window =
      glfwCreateWindow(1024, 768, "VkPrimer", nullptr, nullptr);
  if (!window) {
    RTN_MSG(1, "glfwCreateWindow failed.\n");
  }
  if (!vulkan_instance->Init(kEnableValidation, window)) {
    RTN_MSG(1, "Instance Initialization Failed.\n");
  }
#else
  if (!vulkan_instance->Init(kEnableValidation)) {
    RTN_MSG(1, "Instance Initialization Failed.\n");
  }
#endif

  // LAYERS
  VulkanLayer vulkan_layer(vulkan_instance);
  if (!vulkan_layer.Init()) {
    RTN_MSG(1, "Layer Initialization Failed.\n");
  }

  // SURFACE
#if USE_GLFW
  auto surface = std::make_unique<VulkanSurface>(vulkan_instance, window);
#else
  auto surface = std::make_unique<VulkanSurface>(vulkan_instance);
#endif
  if (!surface->Init()) {
    RTN_MSG(1, "Surface Initialization Failed.\n");
  }

  // PHYSICAL DEVICE
  VulkanPhysicalDevice physical_device(vulkan_instance, surface->surface());
  if (!physical_device.Init()) {
    RTN_MSG(1, "Phys Device Initialization Failed.\n");
  }

  // LOGICAL DEVICE
  auto logical_device = std::make_shared<VulkanLogicalDevice>(
      physical_device.phys_device(), surface->surface(), kEnableValidation);
  if (!logical_device->Init()) {
    RTN_MSG(1, "Logical Device Initialization Failed.\n");
  }

  // SWAP CHAIN
  auto swap_chain = std::make_unique<VulkanSwapchain>(
      physical_device.phys_device(), logical_device, surface->surface());
  if (!swap_chain->Init()) {
    RTN_MSG(1, "Swap Chain Initialization Failed.\n");
  }

  // RENDER PASS
  auto render_pass = std::make_unique<VulkanRenderPass>(
      logical_device, swap_chain->image_format());
  if (!render_pass->Init()) {
    RTN_MSG(1, "Render Pass Initialization Failed.\n");
  }

  // GRAPHICS PIPELINE
  auto graphics_pipeline = std::make_unique<VulkanGraphicsPipeline>(
      logical_device, swap_chain->extent(), render_pass->render_pass());
  if (!graphics_pipeline->Init()) {
    RTN_MSG(1, "Graphics Pipeline Initialization Failed.\n");
  }

  // FRAMEBUFFER
  auto framebuffer = std::make_unique<VulkanFramebuffer>(
      logical_device, swap_chain->image_views(), swap_chain->extent(),
      render_pass->render_pass());
  if (!framebuffer->Init()) {
    RTN_MSG(1, "Framebuffer Initialization Failed.\n");
  }

  // COMMAND POOL
  auto command_pool = std::make_shared<VulkanCommandPool>(
      logical_device, physical_device.phys_device(), surface->surface());
  if (!command_pool->Init()) {
    RTN_MSG(1, "Command Pool Initialization Failed.\n");
  }

  // COMMAND BUFFER
  auto command_buffers = std::make_unique<VulkanCommandBuffers>(
      logical_device, command_pool, framebuffer->framebuffers(),
      swap_chain->extent(), render_pass->render_pass(),
      graphics_pipeline->graphics_pipeline());
  if (!command_buffers->Init()) {
    RTN_MSG(1, "Command Buffer Initialization Failed.\n");
  }

  // SYNC
  auto sync = std::make_unique<VulkanSync>(logical_device,
                                           3 /* max_frames_in_flight */);
  if (!sync->Init()) {
    RTN_MSG(1, "Sync Initialization Failed.\n");
  }

#if USE_GLFW
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    DrawFrame(*logical_device, *sync, *swap_chain, *command_buffers);
  }
#else
  DrawFrame(*logical_device, *sync, *swap_chain, *command_buffers);
  sleep(3);
#endif
  vkDeviceWaitIdle(logical_device->device());

#if USE_GLFW
  glfwDestroyWindow(window);
  glfwTerminate();
#endif

  return 0;
}

bool DrawFrame(const VulkanLogicalDevice& logical_device,
               const VulkanSync& sync, const VulkanSwapchain& swap_chain,
               const VulkanCommandBuffers& command_buffers) {
  static int current_frame = 0;

  // Compact variables for readability derived from |current_frame|.
  const VkDevice& device = logical_device.device();

  const VkFence& fence = sync.in_flight_fences()[current_frame];

  const VkSemaphore& image_available_semaphore =
      sync.image_available_semaphores()[current_frame];

  const VkSemaphore& render_finished_semaphore =
      sync.render_finished_semaphores()[current_frame];

  // Wait for any outstanding command buffers to be processed.
  vkWaitForFences(device, 1, &fence, VK_TRUE,
                  std::numeric_limits<uint64_t>::max());
  vkResetFences(device, 1, &fence);

  // Obtain next swap chain image in which to draw.
  uint32_t image_index;
  vkAcquireNextImageKHR(
      device, swap_chain.swap_chain(), std::numeric_limits<uint64_t>::max(),
      image_available_semaphore, VK_NULL_HANDLE, &image_index);

  // Define stage that |image_available_semaphore| is waiting on.
  const VkPipelineStageFlags image_available_wait_stage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &image_available_semaphore,
      .pWaitDstStageMask = &image_available_wait_stage,
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffers.command_buffers()[image_index],
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &render_finished_semaphore,
  };

  if (vkQueueSubmit(logical_device.queue(), 1, &submit_info, fence) !=
      VK_SUCCESS) {
    RTN_MSG(false, "Failed to submit draw command buffer.\n");
  }

  VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &render_finished_semaphore,
      .swapchainCount = 1,
      .pSwapchains = &swap_chain.swap_chain(),
      .pImageIndices = &image_index,
  };

  vkQueuePresentKHR(logical_device.queue(), &present_info);

  current_frame = (current_frame + 1) % sync.max_frames_in_flight();

  return true;
}
