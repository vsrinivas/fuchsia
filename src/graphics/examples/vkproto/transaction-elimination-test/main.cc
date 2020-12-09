// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "hwcpipe.h"
#include "src/graphics/examples/vkproto/common/command_buffers.h"
#include "src/graphics/examples/vkproto/common/command_pool.h"
#include "src/graphics/examples/vkproto/common/debug_utils_messenger.h"
#include "src/graphics/examples/vkproto/common/device.h"
#include "src/graphics/examples/vkproto/common/framebuffers.h"
#include "src/graphics/examples/vkproto/common/image_view.h"
#include "src/graphics/examples/vkproto/common/instance.h"
#include "src/graphics/examples/vkproto/common/physical_device.h"
#include "src/graphics/examples/vkproto/common/pipeline.h"
#include "src/graphics/examples/vkproto/common/render_pass.h"
#include "src/graphics/examples/vkproto/common/swapchain.h"
#include "src/graphics/examples/vkproto/common/utils.h"

#include <vulkan/vulkan.hpp>

uint32_t GetCounterValue(const hwcpipe::GpuMeasurements* gpu, hwcpipe::GpuCounter counter) {
  auto it = gpu->find(counter);
  EXPECT_NE(it, gpu->end());
  return it->second.get<uint32_t>();
}

static bool DrawAllFrames(const vkp::Device& vkp_device,
                          const vkp::CommandBuffers& vkp_command_buffers);

static void InitCommandBuffers(const vk::Image* image_for_foreign_transition, uint32_t queue_family,
                               std::unique_ptr<vkp::CommandBuffers>& vkp_command_buffers) {
  vk::ClearValue clear_color;
  clear_color.color = std::array<float, 4>({0.5f, 0.0f, 0.5f, 1.0f});
  vk::RenderPassBeginInfo render_pass_info;
  render_pass_info.renderPass = vkp_command_buffers->render_pass();
  render_pass_info.renderArea = vk::Rect2D(0 /* offset */, vkp_command_buffers->extent());
  render_pass_info.clearValueCount = 1;
  render_pass_info.pClearValues = &clear_color;

  const vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eSimultaneousUse,
                                              nullptr /* pInheritanceInfo */);
  const std::vector<vk::UniqueCommandBuffer>& command_buffers =
      vkp_command_buffers->command_buffers();
  for (size_t i = 0; i < command_buffers.size(); ++i) {
    const vk::CommandBuffer& command_buffer = command_buffers[i].get();
    ASSERT_TRUE(command_buffer.begin(&begin_info) == vk::Result::eSuccess);
    render_pass_info.framebuffer = vkp_command_buffers->framebuffers()[i].get();

    // Record commands to render pass.
    command_buffer.beginRenderPass(&render_pass_info, vk::SubpassContents::eInline);
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                vkp_command_buffers->graphics_pipeline());
    command_buffer.draw(3 /* vertexCount */, 1 /* instanceCount */, 0 /* firstVertex */,
                        0 /* firstInstance */);
    command_buffer.endRenderPass();

    if (image_for_foreign_transition) {
      vk::ImageMemoryBarrier barrier;
      barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
          .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
          .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
          .setSrcQueueFamilyIndex(queue_family)
          .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_EXTERNAL)
          .setSubresourceRange(vk::ImageSubresourceRange()
                                   .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                   .setLevelCount(1)
                                   .setLayerCount(1))
          .setImage(*image_for_foreign_transition);

      command_buffer.pipelineBarrier(
          vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics,
          vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* memoryBarriers */,
          0 /* bufferMemoryBarrierCount */, nullptr /* bufferMemoryBarriers */,
          1 /* imageMemoryBarrierCount */, &barrier);

      // This barrier should transition it back
      vk::ImageMemoryBarrier barrier2(barrier);
      command_buffer.pipelineBarrier(
          vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics,
          vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* memoryBarriers */,
          0 /* bufferMemoryBarrierCount */, nullptr /* bufferMemoryBarriers */,
          1 /* imageMemoryBarrierCount */, &barrier2);
    }

    command_buffer.end();
  }
}

// Test that transferring an image to a foreign queue and back doesn't prevent transaction
// elimination from working.
TEST(TransactionElimination, ForeignQueue) {
  vkp::Instance vkp_instance(vkp::Instance::Builder()
                                 .set_validation_layers_enabled(true)
                                 .set_extensions({
                                     VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
                                     VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
                                 })
                                 .Build());

  vkp::DebugUtilsMessenger vkp_debug_messenger(vkp_instance.shared());
  ASSERT_TRUE(vkp_debug_messenger.Init());

  vkp::PhysicalDevice vkp_physical_device(vkp_instance.shared());
  ASSERT_TRUE(vkp_physical_device.Init());

  vkp::Device vkp_device(vkp_physical_device.get());
  ASSERT_TRUE(vkp_device.Init());
  std::shared_ptr<vk::Device> device = vkp_device.shared();

  vk::Format image_format;
  vk::Extent2D extent;

  std::vector<vk::ImageView> image_views;
  std::shared_ptr<vkp::ImageView> vkp_offscreen_image_view;
  vkp_offscreen_image_view =
      std::make_shared<vkp::ImageView>(device, vkp_physical_device.get(), vk::Extent2D{64, 64});
  ASSERT_TRUE(vkp_offscreen_image_view->Init());

  image_format = vkp_offscreen_image_view->format();
  extent = vkp_offscreen_image_view->extent();
  image_views.emplace_back(vkp_offscreen_image_view->get());

  auto vkp_render_pass = std::make_shared<vkp::RenderPass>(device, image_format, true);
  ASSERT_TRUE(vkp_render_pass->Init());

  auto vkp_pipeline = std::make_unique<vkp::Pipeline>(device, extent, vkp_render_pass);
  ASSERT_TRUE(vkp_pipeline->Init());

  auto vkp_framebuffer =
      std::make_unique<vkp::Framebuffers>(device, extent, vkp_render_pass->get(), image_views);
  ASSERT_TRUE(vkp_framebuffer->Init());

  auto vkp_command_pool =
      std::make_shared<vkp::CommandPool>(device, vkp_device.queue_family_index());
  ASSERT_TRUE(vkp_command_pool->Init());

  // First command buffer does a transition to queue family foreign and back.
  auto vkp_command_buffers = std::make_unique<vkp::CommandBuffers>(
      device, vkp_command_pool, vkp_framebuffer->framebuffers(), extent, vkp_render_pass->get(),
      vkp_pipeline->get());
  ASSERT_TRUE(vkp_command_buffers->Alloc());
  InitCommandBuffers(&(vkp_offscreen_image_view->image().get()), vkp_device.queue_family_index(),
                     vkp_command_buffers);

  hwcpipe::HWCPipe pipe;
  pipe.set_enabled_gpu_counters(pipe.gpu_profiler()->supported_counters());
  pipe.run();

  ASSERT_TRUE(DrawAllFrames(vkp_device, *vkp_command_buffers));
  vkp_device.get().waitIdle();
  auto sample = pipe.sample();
  EXPECT_EQ(0u, GetCounterValue(sample.gpu, hwcpipe::GpuCounter::TransactionEliminations));

  // Second render pass and command buffers do a transition from eTransferSrcOptimal instead of
  // eUndefined, since otherwise transaction elimination would be disabled.
  auto vkp_render_pass2 = std::make_shared<vkp::RenderPass>(device, image_format, true);
  vkp_render_pass2->set_initial_layout(vk::ImageLayout::eTransferSrcOptimal);
  ASSERT_TRUE(vkp_render_pass2->Init());

  auto vkp_command_buffers2 = std::make_unique<vkp::CommandBuffers>(
      device, vkp_command_pool, vkp_framebuffer->framebuffers(), extent, vkp_render_pass2->get(),
      vkp_pipeline->get());
  ASSERT_TRUE(vkp_command_buffers2->Alloc());
  InitCommandBuffers({} /* image_for_foreign_transition */, {} /* queue_family */,
                     vkp_command_buffers2);

  ASSERT_TRUE(DrawAllFrames(vkp_device, *vkp_command_buffers2));
  vkp_device.get().waitIdle();
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
