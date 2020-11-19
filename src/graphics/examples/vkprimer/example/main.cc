// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <cassert>
#include <limits>
#include <memory>
#include <vector>

#include "device.h"
#include "utils.h"
#include "vulkan_command_buffers.h"
#include "vulkan_command_pool.h"
#include "vulkan_framebuffer.h"
#include "vulkan_graphics_pipeline.h"
#include "vulkan_image_view.h"
#include "vulkan_instance.h"
#include "vulkan_layer.h"
#include "vulkan_physical_device.h"
#include "vulkan_render_pass.h"
#include "vulkan_surface.h"
#include "vulkan_swapchain.h"

#include <vulkan/vulkan.hpp>

#if USE_GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

static bool DrawFrame(const vkp::Device& vkp_device, const VulkanSwapchain& swap_chain,
                      const VulkanCommandBuffers& command_buffers,
                      const std::vector<vk::UniqueFence>& fences);

static bool DrawOffscreenFrame(const vkp::Device& vkp_device,
                               const VulkanCommandBuffers& command_buffers, const vk::Fence& fence);

static bool Readback(const vkp::Device& vkp_device, const VulkanImageView& image_view);

void glfwErrorCallback(int error, const char* description) {
  fprintf(stderr, "glfwErrorCallback: %d : %s\n", error, description);
}

int main(int argc, char* argv[]) {
  const bool offscreen = (argc == 2 && !strcmp(argv[1], "-offscreen"));
  printf("Is Offscreen: %s\n", offscreen ? "yes" : "no");

  // INSTANCE
  const bool kEnableValidation = true;
  auto instance = std::make_shared<VulkanInstance>();
#if USE_GLFW
  glfwInit();
  glfwSetErrorCallback(glfwErrorCallback);
  RTN_IF_MSG(1, !glfwVulkanSupported(), "glfwVulkanSupported has returned false.\n");
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(1024, 768, "VkPrimer", nullptr, nullptr);
  RTN_IF_MSG(1, !window, "glfwCreateWindow failed.\n");
  RTN_IF_MSG(1, !instance->Init(kEnableValidation, window), "Instance Initialization Failed.\n");
#else
  RTN_IF_MSG(1, !instance->Init(kEnableValidation), "Instance Initialization Failed.\n");
#endif

  // LAYERS
  VulkanLayer vulkan_layer(instance);
  RTN_IF_MSG(1, !vulkan_layer.Init(), "Layer Initialization Failed.\n");

// SURFACE
#if USE_GLFW
  auto surface = std::make_shared<VulkanSurface>(instance, window);
#else
  auto surface = std::make_shared<VulkanSurface>(instance);
#endif
  RTN_IF_MSG(1, !surface->Init(), "Surface initialization failed\n");

  // PHYSICAL DEVICE
  auto physical_device = std::make_shared<VulkanPhysicalDevice>(instance, surface->surface());
  RTN_IF_MSG(1, !physical_device->Init(), "Physical device initialization failed\n");

  // LOGICAL DEVICE
  auto vkp_device = std::make_shared<vkp::Device>(physical_device->phys_device(),
                                                  surface->surface(), kEnableValidation);
  RTN_IF_MSG(1, !vkp_device->Init(), "Logical device initialization failed\n");

  vk::Format image_format;
  vk::Extent2D extent;
  std::shared_ptr<VulkanSwapchain> swap_chain;

  // The number of image views added in either the offscreen or onscreen logic blocks
  // below controls the number of framebuffers, command buffers, fences and signalling
  // semaphores created subsequently.
  std::vector<vk::ImageView> image_views;
  std::shared_ptr<VulkanImageView> offscreen_image_view;
  if (offscreen) {
    // IMAGE VIEW
    offscreen_image_view = std::make_shared<VulkanImageView>(vkp_device, physical_device);
    RTN_IF_MSG(1, !offscreen_image_view->Init(), "Image View initialization failed\n");
    image_format = offscreen_image_view->format();
    extent = offscreen_image_view->extent();
    image_views.emplace_back(*(offscreen_image_view->view()));
  } else {
    // SWAP CHAIN
    swap_chain =
        std::make_shared<VulkanSwapchain>(physical_device->phys_device(), vkp_device, surface);
    RTN_IF_MSG(1, !swap_chain->Init(), "Swap chain initialization failed\n");

    image_format = swap_chain->image_format();
    extent = swap_chain->extent();
    const auto& swap_chain_image_views = swap_chain->image_views();
    for (auto& view : swap_chain_image_views) {
      image_views.emplace_back(*view);
    }
  }

  // RENDER PASS
  auto render_pass = std::make_shared<VulkanRenderPass>(vkp_device, image_format, offscreen);
  RTN_IF_MSG(1, !render_pass->Init(), "Render pass initialization failed\n");

  // GRAPHICS PIPELINE
  auto graphics_pipeline =
      std::make_unique<VulkanGraphicsPipeline>(vkp_device, extent, render_pass);
  RTN_IF_MSG(1, !graphics_pipeline->Init(), "Graphics pipeline initialization failed\n");

  // FRAMEBUFFER
  auto framebuffer = std::make_unique<VulkanFramebuffer>(vkp_device, extent,
                                                         *render_pass->render_pass(), image_views);
  RTN_IF_MSG(1, !framebuffer->Init(), "Framebuffer Initialization Failed.\n");

  // COMMAND POOL
  auto command_pool = std::make_shared<VulkanCommandPool>(
      vkp_device, physical_device->phys_device(), surface->surface());
  RTN_IF_MSG(1, !command_pool->Init(), "Command Pool Initialization Failed.\n");

  // COMMAND BUFFER
  auto command_buffers = std::make_unique<VulkanCommandBuffers>(
      vkp_device, command_pool, *framebuffer, extent, *render_pass->render_pass(),
      graphics_pipeline->graphics_pipeline());
  RTN_IF_MSG(1, !command_buffers->Init(), "Command buffer initialization.\n");

  // Offscreen drawing submission fence.
  const vk::Device& device = vkp_device->get();
  const vk::FenceCreateInfo fence_info(vk::FenceCreateFlagBits::eSignaled);
  auto [r_offscren_fence, offscreen_fence] = device.createFenceUnique(fence_info);
  RTN_IF_VKH_ERR(1, r_offscren_fence, "Offscreen submission fence.\n");

  // Onscreen drawing submission fences.
  // There is a 1/1/1 mapping between swapchain image view / command buffer / fence.
  std::vector<vk::UniqueFence> fences;
  for (size_t i = 0; i < image_views.size(); i++) {
    auto [r_fence, fence] = device.createFenceUnique(fence_info);
    RTN_IF_VKH_ERR(1, r_fence, "Onscreen submission fence.\n");
    fences.emplace_back(std::move(fence));
  }

#if USE_GLFW
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    if (offscreen) {
      DrawOffscreenFrame(*vkp_device, *command_buffers, offscreen_fence.get());
    } else {
      DrawFrame(*vkp_device, *swap_chain, *command_buffers, fences);
    }
  }
#else
  if (offscreen) {
    DrawOffscreenFrame(*vkp_device, *command_buffers, offscreen_fence.get());
  } else {
    DrawFrame(*vkp_device, *swap_chain, *command_buffers, fences);
  }
  sleep(3);
#endif
  device.waitIdle();

  if (offscreen) {
    Readback(*vkp_device, *offscreen_image_view);
  }

#if USE_GLFW
  glfwDestroyWindow(window);
  glfwTerminate();
#endif

  return 0;
}

bool DrawFrame(const vkp::Device& vkp_device, const VulkanSwapchain& swap_chain,
               const VulkanCommandBuffers& command_buffers,
               const std::vector<vk::UniqueFence>& fences) {
  // Compact variables for readability derived from |current_frame|.
  const vk::Device& device = vkp_device.get();

  auto [r_image_available_semaphore, image_available_semaphore] =
      device.createSemaphore(vk::SemaphoreCreateInfo{});
  RTN_IF_VKH_ERR(false, r_image_available_semaphore, "Image available semaphore.\n");

  auto [r_render_finished_semaphore, render_finished_semaphore] =
      device.createSemaphore(vk::SemaphoreCreateInfo{});
  RTN_IF_VKH_ERR(false, r_render_finished_semaphore, "Render finished semaphore.\n");

  // Obtain next swap chain image in which to draw.
  // The timeout makes this a blocking call if no swapchain images, and therefore
  // command buffers, are available so there is no need to wait for a submission fence
  // before calling acquireNextImageKHR().
  auto [r_acquire, swapchain_image_index] =
      device.acquireNextImageKHR(*swap_chain.swap_chain(), std::numeric_limits<uint64_t>::max(),
                                 image_available_semaphore, nullptr);
  RTN_IF_VKH_ERR(false, r_acquire, "Acquire swapchain image.\n");

  // Define stage that |image_available_semaphore| is waiting on.
  const vk::PipelineStageFlags image_available_wait_stage =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;

  vk::CommandBuffer command_buffer = command_buffers.command_buffers()[swapchain_image_index].get();

  vk::SubmitInfo submit_info;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &image_available_semaphore;
  submit_info.pWaitDstStageMask = &image_available_wait_stage;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &render_finished_semaphore;

  // No guarantees that we're done with the acquired swap chain image and therefore
  // the command buffer we're about to use so wait on the command buffer's fence.
  const vk::Fence& fence = fences[swapchain_image_index].get();
  device.waitForFences(1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
  device.resetFences(1, &fence);

  RTN_IF_VKH_ERR(false, vkp_device.queue().submit(1, &submit_info, fence),
                 "Failed to onscreen submit command buffer.\n");

  vk::PresentInfoKHR present_info;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &render_finished_semaphore;
  present_info.swapchainCount = 1;
  present_info.setPSwapchains(&(swap_chain.swap_chain().get()));
  present_info.pImageIndices = &swapchain_image_index;

  vkp_device.queue().presentKHR(&present_info);

  return true;
}

bool DrawOffscreenFrame(const vkp::Device& vkp_device, const VulkanCommandBuffers& command_buffers,
                        const vk::Fence& fence) {
  vk::CommandBuffer command_buffer = command_buffers.command_buffers()[0].get();
  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;

  // Wait for any outstanding command buffers to be processed.
  const vk::Device& device = vkp_device.get();
  device.waitForFences(1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
  device.resetFences(1, &fence);

  RTN_IF_VKH_ERR(false, vkp_device.queue().submit(1, &submit_info, fence),
                 "Failed to offscreen submit command buffer.\n");
  return true;
}

bool Readback(const vkp::Device& vkp_device, const VulkanImageView& image_view) {
  const vk::Device& device = vkp_device.get();
  vk::DeviceMemory device_memory = *(image_view.image_memory());
  auto rv = device.mapMemory(device_memory, 0 /* offset */, VK_WHOLE_SIZE,
                             static_cast<vk::MemoryMapFlags>(0));
  RTN_IF_VKH_ERR(false, rv.result, "Memory map failed.\n");
  uint8_t* image_buffer = static_cast<uint8_t*>(rv.value);
  printf("Clear Color Read Back: (%02x,%02x,%02x,%02x)\n", *(image_buffer + 0), *(image_buffer + 1),
         *(image_buffer + 2), *(image_buffer + 3));
  device.unmapMemory(device_memory);

  return true;
}
