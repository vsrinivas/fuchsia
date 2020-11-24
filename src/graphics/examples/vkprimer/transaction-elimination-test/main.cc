// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "hwcpipe.h"
#include "src/graphics/examples/vkprimer/common/command_buffers.h"
#include "src/graphics/examples/vkprimer/common/command_pool.h"
#include "src/graphics/examples/vkprimer/common/device.h"
#include "src/graphics/examples/vkprimer/common/framebuffers.h"
#include "src/graphics/examples/vkprimer/common/image_view.h"
#include "src/graphics/examples/vkprimer/common/instance.h"
#include "src/graphics/examples/vkprimer/common/physical_device.h"
#include "src/graphics/examples/vkprimer/common/pipeline.h"
#include "src/graphics/examples/vkprimer/common/render_pass.h"
#ifdef __Fuchsia__
#include "src/graphics/examples/vkprimer/fuchsia/surface.h"
#else
#include "src/graphics/examples/vkprimer/glfw/surface.h"
#endif
#include "src/graphics/examples/vkprimer/common/swapchain.h"
#include "src/graphics/examples/vkprimer/common/utils.h"

#include <vulkan/vulkan.hpp>

uint32_t GetCounterValue(const hwcpipe::GpuMeasurements* gpu, hwcpipe::GpuCounter counter) {
  auto it = gpu->find(counter);
  EXPECT_NE(it, gpu->end());
  return it->second.get<uint32_t>();
}

static bool DrawAllFrames(const vkp::Device& vkp_device,
                          const vkp::CommandBuffers& vkp_command_buffers);

// Test that transfering an image to a foreign queue and back doesn't prevent transaction
// elimination from working.
TEST(TransactionElimination, ForeignQueue) {
  const bool kEnableValidation = true;
  auto vkp_instance = std::make_shared<vkp::Instance>(kEnableValidation);
  ASSERT_TRUE(vkp_instance->Init());

  auto vkp_surface = std::make_shared<vkp::Surface>(vkp_instance);
  ASSERT_TRUE(vkp_surface->Init());

  auto vkp_physical_device =
      std::make_shared<vkp::PhysicalDevice>(vkp_instance, vkp_surface->get());
  ASSERT_TRUE(vkp_physical_device->Init());

  auto vkp_device = std::make_shared<vkp::Device>(vkp_physical_device->get(), vkp_surface->get());
  ASSERT_TRUE(vkp_device->Init());

  vk::Format image_format;
  vk::Extent2D extent;

  std::vector<vk::ImageView> image_views;
  std::shared_ptr<vkp::ImageView> vkp_offscreen_image_view;
  vkp_offscreen_image_view =
      std::make_shared<vkp::ImageView>(vkp_device, vkp_physical_device, vk::Extent2D{64, 64});
  ASSERT_TRUE(vkp_offscreen_image_view->Init());

  image_format = vkp_offscreen_image_view->format();
  extent = vkp_offscreen_image_view->extent();
  image_views.emplace_back(vkp_offscreen_image_view->get());

  auto vkp_render_pass = std::make_shared<vkp::RenderPass>(vkp_device, image_format, true);
  ASSERT_TRUE(vkp_render_pass->Init());

  auto vkp_pipeline = std::make_unique<vkp::Pipeline>(vkp_device, extent, vkp_render_pass);
  ASSERT_TRUE(vkp_pipeline->Init());

  auto vkp_framebuffer =
      std::make_unique<vkp::Framebuffers>(vkp_device, extent, vkp_render_pass->get(), image_views);
  ASSERT_TRUE(vkp_framebuffer->Init());

  auto vkp_command_pool = std::make_shared<vkp::CommandPool>(vkp_device, vkp_physical_device->get(),
                                                             vkp_surface->get());
  ASSERT_TRUE(vkp_command_pool->Init());

  // First command buffer does a transition to queue family foreign and back.
  auto vkp_command_buffers = std::make_unique<vkp::CommandBuffers>(
      vkp_device, vkp_command_pool, vkp_framebuffer->framebuffers(), extent, vkp_render_pass->get(),
      vkp_pipeline->get());
  vkp_command_buffers->set_image_for_foreign_transition(*vkp_offscreen_image_view->image());
  ASSERT_TRUE(vkp_command_buffers->Init());

  hwcpipe::HWCPipe pipe;
  pipe.set_enabled_gpu_counters(pipe.gpu_profiler()->supported_counters());
  pipe.run();

  ASSERT_TRUE(DrawAllFrames(*vkp_device, *vkp_command_buffers));
  vkp_device->get().waitIdle();
  auto sample = pipe.sample();
  EXPECT_EQ(0u, GetCounterValue(sample.gpu, hwcpipe::GpuCounter::TransactionEliminations));

  // Second render pass and command buffers do a transition from eTransferSrcOptimal instead of
  // eUndefined, since otherwise transaction elimination would be disabled.
  auto vkp_render_pass2 = std::make_shared<vkp::RenderPass>(vkp_device, image_format, true);
  vkp_render_pass2->set_initial_layout(vk::ImageLayout::eTransferSrcOptimal);
  ASSERT_TRUE(vkp_render_pass2->Init());

  auto vkp_command_buffers2 = std::make_unique<vkp::CommandBuffers>(
      vkp_device, vkp_command_pool, vkp_framebuffer->framebuffers(), extent,
      vkp_render_pass2->get(), vkp_pipeline->get());
  ASSERT_TRUE(vkp_command_buffers2->Init());

  ASSERT_TRUE(DrawAllFrames(*vkp_device, *vkp_command_buffers2));
  vkp_device->get().waitIdle();
  auto sample2 = pipe.sample();
  constexpr uint32_t kTransactionMinTileSize = 16;
  constexpr uint32_t kTransactionMaxTileSize = 32;
  uint32_t eliminated_count =
      GetCounterValue(sample2.gpu, hwcpipe::GpuCounter::TransactionEliminations);
  // All transactions should be eliminated.
  EXPECT_GE((64u / kTransactionMinTileSize) * (64u / kTransactionMinTileSize), eliminated_count);
  EXPECT_LE((64u / kTransactionMaxTileSize) * (64u / kTransactionMaxTileSize), eliminated_count);
}

bool DrawAllFrames(const vkp::Device& vkp_device, const vkp::CommandBuffers& vkp_command_buffers) {
  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = vkp_command_buffers.command_buffers().size();
  std::vector<vk::CommandBuffer> command_buffer(submit_info.commandBufferCount);
  for (uint32_t i = 0; i < submit_info.commandBufferCount; i++) {
    command_buffer[i] = vkp_command_buffers.command_buffers()[i].get();
  }
  submit_info.pCommandBuffers = command_buffer.data();

  if (vkp_device.queue().submit(1, &submit_info, vk::Fence()) != vk::Result::eSuccess) {
    RTN_MSG(false, "Failed to submit draw command buffer.\n");
  }
  return true;
}
