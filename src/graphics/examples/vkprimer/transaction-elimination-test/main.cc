// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "hwcpipe.h"
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

uint32_t GetCounterValue(const hwcpipe::GpuMeasurements* gpu, hwcpipe::GpuCounter counter) {
  auto it = gpu->find(counter);
  EXPECT_NE(it, gpu->end());
  return it->second.get<uint32_t>();
}

static bool DrawAllFrames(const VulkanLogicalDevice& logical_device,
                          const VulkanCommandBuffers& command_buffers);

// Test that transfering an image to a foreign queue and back doesn't prevent transaction
// elimination from working.
TEST(TransactionElimination, ForeignQueue) {
  const bool kEnableValidation = true;
  auto instance = std::make_shared<VulkanInstance>();
  ASSERT_TRUE(instance->Init(kEnableValidation));

  VulkanLayer vulkan_layer(instance);
  ASSERT_TRUE(vulkan_layer.Init());

  auto surface = std::make_shared<VulkanSurface>(instance);
  ASSERT_TRUE(surface->Init());

  auto physical_device = std::make_shared<VulkanPhysicalDevice>(instance, surface->surface());
  ASSERT_TRUE(physical_device->Init());

  auto logical_device = std::make_shared<VulkanLogicalDevice>(
      physical_device->phys_device(), surface->surface(), kEnableValidation);
  ASSERT_TRUE(logical_device->Init());

  vk::Format image_format;
  vk::Extent2D extent;

  std::vector<vk::ImageView> image_views;
  std::shared_ptr<VulkanImageView> offscreen_image_view;
  offscreen_image_view =
      std::make_shared<VulkanImageView>(logical_device, physical_device, vk::Extent2D{64, 64});
  ASSERT_TRUE(offscreen_image_view->Init());

  image_format = offscreen_image_view->format();
  extent = offscreen_image_view->extent();
  image_views.emplace_back(*(offscreen_image_view->view()));

  auto render_pass = std::make_shared<VulkanRenderPass>(logical_device, image_format, true);
  ASSERT_TRUE(render_pass->Init());

  auto graphics_pipeline =
      std::make_unique<VulkanGraphicsPipeline>(logical_device, extent, render_pass);
  ASSERT_TRUE(graphics_pipeline->Init());

  auto framebuffer = std::make_unique<VulkanFramebuffer>(logical_device, extent,
                                                         *render_pass->render_pass(), image_views);
  ASSERT_TRUE(framebuffer->Init());

  auto command_pool = std::make_shared<VulkanCommandPool>(
      logical_device, physical_device->phys_device(), surface->surface());
  ASSERT_TRUE(command_pool->Init());

  // First command buffer does a transition to queue family foreign and back.
  auto command_buffers = std::make_unique<VulkanCommandBuffers>(
      logical_device, command_pool, *framebuffer, extent, *render_pass->render_pass(),
      *graphics_pipeline->graphics_pipeline());
  command_buffers->set_image_for_foreign_transition(*offscreen_image_view->image());
  ASSERT_TRUE(command_buffers->Init());

  hwcpipe::HWCPipe pipe;
  pipe.set_enabled_gpu_counters(pipe.gpu_profiler()->supported_counters());
  pipe.run();

  ASSERT_TRUE(DrawAllFrames(*logical_device, *command_buffers));
  logical_device->device()->waitIdle();
  auto sample = pipe.sample();
  EXPECT_EQ(0u, GetCounterValue(sample.gpu, hwcpipe::GpuCounter::TransactionEliminations));

  // Second render pass and command buffers do a transition from eTransferSrcOptimal instead of
  // eUndefined, since otherwise transaction elimination would be disabled.
  auto render_pass2 = std::make_shared<VulkanRenderPass>(logical_device, image_format, true);
  render_pass2->set_initial_layout(vk::ImageLayout::eTransferSrcOptimal);
  ASSERT_TRUE(render_pass2->Init());

  auto command_buffers2 = std::make_unique<VulkanCommandBuffers>(
      logical_device, command_pool, *framebuffer, extent, *render_pass2->render_pass(),
      *graphics_pipeline->graphics_pipeline());
  ASSERT_TRUE(command_buffers2->Init());

  ASSERT_TRUE(DrawAllFrames(*logical_device, *command_buffers2));
  logical_device->device()->waitIdle();
  auto sample2 = pipe.sample();
  constexpr uint32_t kTransactionMinTileSize = 16;
  constexpr uint32_t kTransactionMaxTileSize = 32;
  uint32_t eliminated_count =
      GetCounterValue(sample2.gpu, hwcpipe::GpuCounter::TransactionEliminations);
  // All transactions should be eliminated.
  EXPECT_GE((64u / kTransactionMinTileSize) * (64u / kTransactionMinTileSize), eliminated_count);
  EXPECT_LE((64u / kTransactionMaxTileSize) * (64u / kTransactionMaxTileSize), eliminated_count);
}

bool DrawAllFrames(const VulkanLogicalDevice& logical_device,
                   const VulkanCommandBuffers& command_buffers) {
  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = command_buffers.command_buffers().size();
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
