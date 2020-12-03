// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <memory>
#include <vector>

#include "src/graphics/examples/vkprimer/common/command_buffers.h"
#include "src/graphics/examples/vkprimer/common/command_pool.h"
#include "src/graphics/examples/vkprimer/common/device.h"
#include "src/graphics/examples/vkprimer/common/framebuffers.h"
#include "src/graphics/examples/vkprimer/common/image_view.h"
#include "src/graphics/examples/vkprimer/common/instance.h"
#include "src/graphics/examples/vkprimer/common/physical_device.h"
#include "src/graphics/examples/vkprimer/common/pipeline.h"
#include "src/graphics/examples/vkprimer/common/render_pass.h"
#include "src/graphics/examples/vkprimer/common/swapchain.h"
#include "src/graphics/examples/vkprimer/common/utils.h"

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
  auto vkp_instance = std::make_shared<vkp::Instance>(kEnableValidation);
  if (!vkp_instance->Init()) {
    RTN_MSG(1, "Instance Initialization Failed.\n");
  }

  // PHYSICAL DEVICE
  auto vkp_physical_device = std::make_shared<vkp::PhysicalDevice>(vkp_instance);
  if (!vkp_physical_device->Init()) {
    RTN_MSG(1, "Phys Device Initialization Failed.\n");
  }

  // LOGICAL DEVICE
  vkp::Device vkp_device(vkp_physical_device->get());
  if (!vkp_device.Init()) {
    RTN_MSG(1, "Logical Device Initialization Failed.\n");
  }

  vk::Format image_format;
  vk::Extent2D extent;

  // The number of image views added in either the offscreen or onscreen logic blocks
  // below controls the number of framebuffers, command buffers, fences and signalling
  // semaphores created subsequently.
  std::vector<vk::ImageView> image_views;
  std::shared_ptr<vkp::ImageView> offscreen_image_view;
  std::vector<std::shared_ptr<vkp::ImageView>> vkp_offscreen_image_views;
  constexpr uint32_t kCommandBufferCount = 100;
  for (uint32_t i = 0; i < kCommandBufferCount; i++) {
    std::shared_ptr<vkp::ImageView> offscreen_image_view;
    // IMAGE VIEW
    offscreen_image_view = std::make_shared<vkp::ImageView>(
        vkp_device.shared(), vkp_physical_device, vk::Extent2D{64, 64});
    if (!offscreen_image_view->Init()) {
      RTN_MSG(1, "Image View Initialization Failed.\n");
    }
    image_format = offscreen_image_view->format();
    extent = offscreen_image_view->extent();
    image_views.emplace_back(offscreen_image_view->get());
    vkp_offscreen_image_views.push_back(std::move(offscreen_image_view));
  }

  // RENDER PASS
  auto vkp_render_pass = std::make_shared<vkp::RenderPass>(vkp_device.shared(), image_format, true);
  if (!vkp_render_pass->Init()) {
    RTN_MSG(1, "Render Pass Initialization Failed.\n");
  }

  // GRAPHICS PIPELINE
  auto vkp_pipeline = std::make_unique<vkp::Pipeline>(vkp_device.shared(), extent, vkp_render_pass);
  if (!vkp_pipeline->Init()) {
    RTN_MSG(1, "Graphics Pipeline Initialization Failed.\n");
  }

  // FRAMEBUFFER
  auto vkp_framebuffer = std::make_unique<vkp::Framebuffers>(vkp_device.shared(), extent,
                                                             vkp_render_pass->get(), image_views);
  if (!vkp_framebuffer->Init()) {
    RTN_MSG(1, "Framebuffers Initialization Failed.\n");
  }

  // COMMAND POOL
  auto vkp_command_pool =
      std::make_shared<vkp::CommandPool>(vkp_device.shared(), vkp_device.queue_family_index());
  if (!vkp_command_pool->Init()) {
    RTN_MSG(1, "Command Pool Initialization Failed.\n");
  }

  // COMMAND BUFFER
  auto vkp_command_buffers = std::make_unique<vkp::CommandBuffers>(
      vkp_device.shared(), vkp_command_pool, vkp_framebuffer->framebuffers(), extent,
      vkp_render_pass->get(), vkp_pipeline->get());
  if (!vkp_command_buffers->Init()) {
    RTN_MSG(1, "Command Buffer Initialization Failed.\n");
  }

  sleep(1);

  // Warm up and force the driver to allocate all the memory it will need for the command buffer.
  if (!DrawAllFrames(vkp_device, *vkp_command_buffers)) {
    RTN_MSG(1, "First DrawAllFrames Failed.\n");
  }

  vkp_device.get().waitIdle();

  auto start_time = std::chrono::steady_clock::now();

  if (!DrawAllFrames(vkp_device, *vkp_command_buffers)) {
    RTN_MSG(1, "Second DrawAllFrames Failed.\n");
  }
  vkp_device.get().waitIdle();
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
