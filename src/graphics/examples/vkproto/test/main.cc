// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/graphics/examples/vkproto/common/command_buffers.h"
#include "src/graphics/examples/vkproto/common/command_pool.h"
#include "src/graphics/examples/vkproto/common/debug_utils_messenger.h"
#include "src/graphics/examples/vkproto/common/graphics_pipeline.h"
#include "src/graphics/examples/vkproto/common/image_view.h"
#include "src/graphics/examples/vkproto/common/instance.h"
#include "src/graphics/examples/vkproto/common/physical_device.h"
#include "src/graphics/examples/vkproto/common/readback.h"
#include "src/graphics/examples/vkproto/common/render_pass.h"
#include "src/graphics/examples/vkproto/common/swapchain.h"
#include "src/graphics/examples/vkproto/common/utils.h"

std::shared_ptr<vk::Device> MakeSharedDevice(const vk::PhysicalDevice& physical_device,
                                             uint32_t* queue_family_index) {
  vkp::Device vkp_device(physical_device);
  EXPECT_TRUE(vkp_device.Init()) << "Logical device initialization failed\n";
  *queue_family_index = vkp_device.queue_family_index();

  return vkp_device.shared();
}

bool DrawOffscreenFrame(const vk::Device& device, const vk::Queue& queue,
                        const vk::CommandBuffer& command_buffer, const vk::Fence& fence) {
  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;

  // Wait for any outstanding command buffers to be processed.
  EXPECT_EQ(vk::Result::eSuccess,
            device.waitForFences(1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()));
  EXPECT_EQ(vk::Result::eSuccess, device.resetFences(1, &fence));

  EXPECT_EQ(vk::Result::eSuccess, queue.submit(1, &submit_info, fence))
      << "Failed to submit command buffer for offscreen draw.\n";
  return true;
}

void TestCommon(const vk::PhysicalDevice& physical_device, std::shared_ptr<vk::Device> device,
                uint32_t queue_family_index) {
  // IMAGE VIEW
  vkp::ImageView vkp_image_view(device, physical_device);
  ASSERT_TRUE(vkp_image_view.Init()) << "Image View initialization failed\n";
  vk::Format image_format = vkp_image_view.format();
  vk::Extent2D extent = vkp_image_view.extent();

  // RENDER PASS
  auto vkp_render_pass =
      std::make_shared<vkp::RenderPass>(device, image_format, true /* offscreen */);
  ASSERT_TRUE(vkp_render_pass->Init()) << "Render pass initialization failed\n";

  // GRAPHICS PIPELINE
  auto vkp_pipeline = std::make_unique<vkp::GraphicsPipeline>(device, extent, vkp_render_pass);
  ASSERT_TRUE(vkp_pipeline->Init()) << "Graphics pipeline initialization failed\n";

  // FRAMEBUFFER
  std::vector<vk::ImageView> image_views = {vkp_image_view.get()};
  auto vkp_framebuffers =
      std::make_unique<vkp::Framebuffers>(device, extent, vkp_render_pass->get(), image_views);
  ASSERT_TRUE(vkp_framebuffers->Init()) << "Framebuffer Initialization Failed.\n";

  // COMMAND POOL
  auto vkp_command_pool = std::make_shared<vkp::CommandPool>(device, queue_family_index);
  ASSERT_TRUE(vkp_command_pool->Init()) << "Command Pool Initialization Failed.\n";

  // COMMAND BUFFER
  auto vkp_command_buffers = std::make_unique<vkp::CommandBuffers>(
      device, vkp_command_pool, vkp_framebuffers->framebuffers(), vkp_pipeline->get(),
      vkp_render_pass->get(), extent);
  ASSERT_TRUE(vkp_command_buffers->Init()) << "Command buffer initialization.\n";

  // SUBMISSION FENCE
  const vk::FenceCreateInfo fence_info(vk::FenceCreateFlagBits::eSignaled);
  auto [r_fence, fence] = device->createFenceUnique(fence_info);
  ASSERT_EQ(r_fence, vk::Result::eSuccess) << "Submission fence.\n";

  vk::CommandBuffer command_buffer = vkp_command_buffers->command_buffers()[0].get();
  vk::Queue queue = device->getQueue(queue_family_index, 0);
  DrawOffscreenFrame(*device, queue, command_buffer, fence.get());
  EXPECT_EQ(vk::Result::eSuccess, device->waitIdle());

  // READBACK
  std::vector<uint8_t> clear_color = {0x7f, 0x00, 0x33, 0xff};
  std::vector<uint32_t> output_pixels(1);
  vkp::ReadPixels(physical_device, *device, *(vkp_image_view.image()), extent,
                  vkp_command_pool->get(), queue, vk::Extent2D{1, 1}, vk::Offset2D{},
                  &output_pixels);
  uint32_t output_pixel = htole32(output_pixels[0]);
  EXPECT_NEAR(clear_color[0], (uint8_t)((output_pixel >> 0) & 0xFF), 1);
  EXPECT_NEAR(clear_color[1], (uint8_t)((output_pixel >> 8) & 0xFF), 1);
  EXPECT_NEAR(clear_color[2], (uint8_t)((output_pixel >> 16) & 0xFF), 1);
  EXPECT_NEAR(clear_color[3], (uint8_t)((output_pixel >> 24) & 0xFF), 1);
}

// Test to verify that destruction of vkp::Device container doesn't affect
// the shared_ptr<vk::Device> ivar within provided a ref ct is maintained.
TEST(VkProto, DisposableVKPContainer) {
  // INSTANCE
  const bool kEnableValidation = true;
  vkp::Instance vkp_instance(kEnableValidation);
  ASSERT_TRUE(vkp_instance.Init()) << "Instance Initialization Failed.\n";

  // DEBUG MESSENGER
  vkp::DebugUtilsMessenger vkp_debug_messenger(vkp_instance.shared());
  ASSERT_TRUE(vkp_debug_messenger.Init());

  // PHYSICAL DEVICE
  vkp::PhysicalDevice vkp_physical_device(vkp_instance.shared());
  ASSERT_TRUE(vkp_physical_device.Init()) << "Physical device initialization failed\n";

  // LOGICAL DEVICE
  uint32_t queue_family_index = 0;
  std::shared_ptr<vk::Device> device =
      MakeSharedDevice(vkp_physical_device.get(), &queue_family_index);

  TestCommon(vkp_physical_device.get(), device, queue_family_index);
}
