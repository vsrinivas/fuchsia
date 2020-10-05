// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <memory>
#include <vector>

#include "utils.h"
#include "vulkan_command_buffers.h"
#include "vulkan_command_pool.h"
#include "vulkan_framebuffer.h"
#include "vulkan_graphics_pipeline.h"
#include "vulkan_image_view.h"
#include "vulkan_instance.h"
#include "vulkan_layer.h"
#include "vulkan_logical_device.h"
#include "vulkan_physical_device.h"
#include "vulkan_render_pass.h"
#include "vulkan_surface.h"
#include "vulkan_swapchain.h"
#include "vulkan_sync.h"

#include <vulkan/vulkan.hpp>

#if USE_GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

void glfwErrorCallback(int error, const char* description) {
  fprintf(stderr, "glfwErrorCallback: %d : %s\n", error, description);
}

static bool DrawAllFrames(const VulkanLogicalDevice& logical_device,
                          const VulkanCommandBuffers& command_buffers);

int main(int argc, char* argv[]) {
// INSTANCE
#ifndef NDEBUG
  printf("Warning - benchmarking debug build.\n");
  const bool kEnableValidation = true;
#else
  const bool kEnableValidation = false;
#endif
  auto instance = std::make_shared<VulkanInstance>();
#if USE_GLFW
  glfwInit();
  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwVulkanSupported()) {
    RTN_MSG(1, "glfwVulkanSupported has returned false.\n");
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(1024, 768, "VkPrimer", nullptr, nullptr);
  if (!window) {
    RTN_MSG(1, "glfwCreateWindow failed.\n");
  }
  if (!instance->Init(kEnableValidation, window)) {
    RTN_MSG(1, "Instance Initialization Failed.\n");
  }
#else
  if (!instance->Init(kEnableValidation)) {
    RTN_MSG(1, "Instance Initialization Failed.\n");
  }
#endif

  // LAYERS
  VulkanLayer vulkan_layer(instance);
  if (!vulkan_layer.Init()) {
    RTN_MSG(1, "Layer Initialization Failed.\n");
  }

  // SURFACE
#if USE_GLFW
  auto surface = std::make_shared<VulkanSurface>(instance, window);
#else
  auto surface = std::make_shared<VulkanSurface>(instance);
#endif
  if (!surface->Init()) {
    RTN_MSG(1, "Surface Initialization Failed.\n");
  }

  // PHYSICAL DEVICE
  auto physical_device = std::make_shared<VulkanPhysicalDevice>(instance, surface->surface());
  if (!physical_device->Init()) {
    RTN_MSG(1, "Phys Device Initialization Failed.\n");
  }

  // LOGICAL DEVICE
  auto logical_device = std::make_shared<VulkanLogicalDevice>(
      physical_device->phys_device(), surface->surface(), kEnableValidation);
  if (!logical_device->Init()) {
    RTN_MSG(1, "Logical Device Initialization Failed.\n");
  }

  vk::Format image_format;
  vk::Extent2D extent;
  std::shared_ptr<VulkanSwapchain> swap_chain;

  // The number of image views added in either the offscreen or onscreen logic blocks
  // below controls the number of framebuffers, command buffers, fences and signalling
  // semaphores created subsequently.
  std::vector<vk::ImageView> image_views;
  std::shared_ptr<VulkanImageView> offscreen_image_view;
  std::vector<std::shared_ptr<VulkanImageView>> offscreen_image_views;
  constexpr uint32_t kCommandBufferCount = 100;
  for (uint32_t i = 0; i < kCommandBufferCount; i++) {
    std::shared_ptr<VulkanImageView> offscreen_image_view;
    // IMAGE VIEW
    offscreen_image_view =
        std::make_shared<VulkanImageView>(logical_device, physical_device, vk::Extent2D{64, 64});
    if (!offscreen_image_view->Init()) {
      RTN_MSG(1, "Image View Initialization Failed.\n");
    }
    image_format = offscreen_image_view->format();
    extent = offscreen_image_view->extent();
    image_views.emplace_back(*(offscreen_image_view->view()));
    offscreen_image_views.push_back(std::move(offscreen_image_view));
  }

  // RENDER PASS
  auto render_pass = std::make_shared<VulkanRenderPass>(logical_device, image_format, true);
  if (!render_pass->Init()) {
    RTN_MSG(1, "Render Pass Initialization Failed.\n");
  }

  // GRAPHICS PIPELINE
  auto graphics_pipeline =
      std::make_unique<VulkanGraphicsPipeline>(logical_device, extent, render_pass);
  if (!graphics_pipeline->Init()) {
    RTN_MSG(1, "Graphics Pipeline Initialization Failed.\n");
  }

  // FRAMEBUFFER
  auto framebuffer = std::make_unique<VulkanFramebuffer>(logical_device, extent,
                                                         *render_pass->render_pass(), image_views);
  if (!framebuffer->Init()) {
    RTN_MSG(1, "Framebuffer Initialization Failed.\n");
  }

  // COMMAND POOL
  auto command_pool = std::make_shared<VulkanCommandPool>(
      logical_device, physical_device->phys_device(), surface->surface());
  if (!command_pool->Init()) {
    RTN_MSG(1, "Command Pool Initialization Failed.\n");
  }

  // COMMAND BUFFER
  auto command_buffers = std::make_unique<VulkanCommandBuffers>(
      logical_device, command_pool, *framebuffer, extent, *render_pass->render_pass(),
      *graphics_pipeline->graphics_pipeline());
  if (!command_buffers->Init()) {
    RTN_MSG(1, "Command Buffer Initialization Failed.\n");
  }

  // SYNC
  auto sync = std::make_unique<VulkanSync>(logical_device, 3 /* max_frames_in_flight */);
  if (!sync->Init()) {
    RTN_MSG(1, "Sync Initialization Failed.\n");
  }
  sleep(1);

  // Warm up and force the driver to allocate all the memory it will need for the command buffer.
  if (!DrawAllFrames(*logical_device, *command_buffers)) {
    RTN_MSG(1, "First DrawAllFrames Failed.\n");
  }

  logical_device->device()->waitIdle();

  auto start_time = std::chrono::steady_clock::now();

  if (!DrawAllFrames(*logical_device, *command_buffers)) {
    RTN_MSG(1, "Second DrawAllFrames Failed.\n");
  }
  logical_device->device()->waitIdle();
  auto end_time = std::chrono::steady_clock::now();

  fprintf(stderr, "End time: %lld\n",
          std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());

#if USE_GLFW
  glfwDestroyWindow(window);
  glfwTerminate();
#endif

  return 0;
}

bool DrawAllFrames(const VulkanLogicalDevice& logical_device,
                   const VulkanCommandBuffers& command_buffers) {
  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = static_cast<uint32_t>(command_buffers.command_buffers().size());
  std::vector<vk::CommandBuffer> command_buffer(submit_info.commandBufferCount);
  for (uint32_t i = 0; i < submit_info.commandBufferCount; i++) {
    command_buffer[i] = command_buffers.command_buffers()[i].get();
  }
  submit_info.pCommandBuffers = command_buffer.data();

  if (logical_device.queue().submit(1, &submit_info, vk::Fence()) != vk::Result::eSuccess) {
    RTN_MSG(false, "Failed to submit draw command buffer.\n");
  }
  return true;
}
