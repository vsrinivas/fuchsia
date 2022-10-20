// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <memory>
#include <vector>

#include "src/graphics/examples/vkproto/common/command_buffers.h"
#include "src/graphics/examples/vkproto/common/command_pool.h"
#include "src/graphics/examples/vkproto/common/debug_utils_messenger.h"
#include "src/graphics/examples/vkproto/common/device.h"
#include "src/graphics/examples/vkproto/common/framebuffers.h"
#include "src/graphics/examples/vkproto/common/graphics_pipeline.h"
#include "src/graphics/examples/vkproto/common/image_view.h"
#include "src/graphics/examples/vkproto/common/instance.h"
#include "src/graphics/examples/vkproto/common/physical_device.h"
#include "src/graphics/examples/vkproto/common/render_pass.h"
#include "src/graphics/examples/vkproto/common/swapchain.h"
#include "src/graphics/examples/vkproto/common/utils.h"

#include <vulkan/vulkan.hpp>

static bool DrawAllFrames(const vkp::Device& vkp_device,
                          const vkp::CommandBuffers& vkp_command_buffers);

int main(int argc, char* argv[]) {
// INSTANCE
#ifndef NDEBUG
  printf("Warning - benchmarking debug build.\n");
  const bool kEnableValidation = true;
#else
  const bool kEnableValidation = false;
#endif
  vkp::Instance vkp_instance(kEnableValidation);
  RTN_IF_MSG(1, !vkp_instance.Init(), "Instance Initialization Failed.\n");

  // DEBUG UTILS MESSENGER
  if (kEnableValidation) {
    vkp::DebugUtilsMessenger vkp_debug_messenger(vkp_instance.shared());
    RTN_IF_MSG(1, !vkp_debug_messenger.Init(), "Debug Messenger Initialization Failed.\n");
  }

  // PHYSICAL DEVICE
  vkp::PhysicalDevice vkp_physical_device(vkp_instance.shared());
  RTN_IF_MSG(1, !vkp_physical_device.Init(), "Phys Device Initialization Failed.\n");

  // LOGICAL DEVICE
  vkp::Device vkp_device(vkp_physical_device.get());
  RTN_IF_MSG(1, !vkp_device.Init(), "Logical Device Initialization Failed.\n");
  std::shared_ptr<vk::Device> device = vkp_device.shared();

  vk::Format image_format;
  vk::Extent2D extent;

  // The number of image views added in either the offscreen or onscreen logic blocks
  // below controls the number of framebuffers, command buffers, fences and signalling
  // semaphores created subsequently.
  std::vector<std::shared_ptr<vkp::ImageView>> vkp_image_views;
  std::vector<vk::ImageView> image_views;
  constexpr uint32_t kCommandBufferCount = 100;
  for (uint32_t i = 0; i < kCommandBufferCount; i++) {
    // IMAGE VIEW
    auto vkp_offscreen_image_view =
        std::make_shared<vkp::ImageView>(device, vkp_physical_device.get(), vk::Extent2D{64, 64});

    RTN_IF_MSG(1, !vkp_offscreen_image_view->Init(), "Image View Initialization Failed.\n");
    image_format = vkp_offscreen_image_view->format();
    extent = vkp_offscreen_image_view->extent();
    image_views.emplace_back(vkp_offscreen_image_view->get());
    vkp_image_views.emplace_back(std::move(vkp_offscreen_image_view));
  }

  // RENDER PASS
  auto vkp_render_pass = std::make_shared<vkp::RenderPass>(device, image_format, true);
  RTN_IF_MSG(1, !vkp_render_pass->Init(), "Render Pass Initialization Failed.\n");

  // GRAPHICS PIPELINE
  auto vkp_pipeline = std::make_unique<vkp::GraphicsPipeline>(device, extent, vkp_render_pass);
  RTN_IF_MSG(1, !vkp_pipeline->Init(), "Graphics Pipeline Initialization Failed.\n");

  // FRAMEBUFFER
  auto vkp_framebuffer =
      std::make_unique<vkp::Framebuffers>(device, extent, vkp_render_pass->get(), image_views);
  RTN_IF_MSG(1, !vkp_framebuffer->Init(), "Framebuffers Initialization Failed.\n");

  // COMMAND POOL
  auto vkp_command_pool =
      std::make_shared<vkp::CommandPool>(device, vkp_device.queue_family_index());
  RTN_IF_MSG(1, !vkp_command_pool->Init(), "Command Pool Initialization Failed.\n");

  // COMMAND BUFFER
  auto vkp_command_buffers = std::make_unique<vkp::CommandBuffers>(
      device, vkp_command_pool, vkp_framebuffer->framebuffers(), vkp_pipeline->get(),
      vkp_render_pass->get(), extent);
  RTN_IF_MSG(1, !vkp_command_buffers->Init(), "Command Buffer Initialization Failed.\n");

  sleep(1);

  // Warm up and force the driver to allocate all the memory it will need for the command buffer.
  if (!DrawAllFrames(vkp_device, *vkp_command_buffers)) {
    RTN_MSG(1, "First DrawAllFrames Failed.\n");
  }

  RTN_IF_MSG(1, vk::Result::eSuccess != device->waitIdle(), "waitIdle failed");

  auto start_time = std::chrono::steady_clock::now();

  if (!DrawAllFrames(vkp_device, *vkp_command_buffers)) {
    RTN_MSG(1, "Second DrawAllFrames Failed.\n");
  }
  RTN_IF_MSG(1, vk::Result::eSuccess != device->waitIdle(), "waitIdle failed");
  auto end_time = std::chrono::steady_clock::now();

  fprintf(stderr, "End time: %lld\n",
          std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());

  return 0;
}

bool DrawAllFrames(const vkp::Device& device, const vkp::CommandBuffers& command_buffers) {
  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = static_cast<uint32_t>(command_buffers.command_buffers().size());
  std::vector<vk::CommandBuffer> command_buffer(submit_info.commandBufferCount);
  for (uint32_t i = 0; i < submit_info.commandBufferCount; i++) {
    command_buffer[i] = command_buffers.command_buffers()[i].get();
  }
  submit_info.pCommandBuffers = command_buffer.data();

  if (device.queue().submit(1, &submit_info, vk::Fence()) != vk::Result::eSuccess) {
    RTN_MSG(false, "Failed to submit draw command buffer.\n");
  }
  return true;
}
