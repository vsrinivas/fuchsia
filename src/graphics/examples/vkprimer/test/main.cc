// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/graphics/examples/vkprimer/common/command_buffers.h"
#include "src/graphics/examples/vkprimer/common/debug_utils_messenger.h"
#include "src/graphics/examples/vkprimer/common/image_view.h"
#include "src/graphics/examples/vkprimer/common/instance.h"
#include "src/graphics/examples/vkprimer/common/physical_device.h"
#include "src/graphics/examples/vkprimer/common/pipeline.h"
#include "src/graphics/examples/vkprimer/common/render_pass.h"
#include "src/graphics/examples/vkprimer/common/swapchain.h"
#include "src/graphics/examples/vkprimer/common/utils.h"

#include <vulkan/vulkan.hpp>

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
  device.waitForFences(1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
  device.resetFences(1, &fence);

  EXPECT_EQ(vk::Result::eSuccess, queue.submit(1, &submit_info, fence))
      << "Failed to offscreen submit command buffer.\n";
  return true;
}

void Readback(const vk::Device& device, const vk::DeviceMemory& device_memory) {
  auto [r_mapped_memory, mapped_memory] =
      device.mapMemory(device_memory, 0 /* offset */, VK_WHOLE_SIZE, vk::MemoryMapFlags{});
  ASSERT_EQ(r_mapped_memory, vk::Result::eSuccess) << "Memory map failed.\n";
  uint8_t* image_buffer = static_cast<uint8_t*>(mapped_memory);
  EXPECT_EQ(0x80, *(image_buffer + 0));
  EXPECT_EQ(0x00, *(image_buffer + 1));
  EXPECT_EQ(0x80, *(image_buffer + 2));
  EXPECT_EQ(0xff, *(image_buffer + 3));
  device.unmapMemory(device_memory);
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
  auto vkp_pipeline = std::make_unique<vkp::Pipeline>(device, extent, vkp_render_pass);
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
      device, vkp_command_pool, vkp_framebuffers->framebuffers(), extent, vkp_render_pass->get(),
      vkp_pipeline->get());
  ASSERT_TRUE(vkp_command_buffers->Init()) << "Command buffer initialization.\n";

  // SUBMISSION FENCE
  const vk::FenceCreateInfo fence_info(vk::FenceCreateFlagBits::eSignaled);
  auto [r_fence, fence] = device->createFenceUnique(fence_info);
  ASSERT_EQ(r_fence, vk::Result::eSuccess) << "Submission fence.\n";

  vk::CommandBuffer command_buffer = vkp_command_buffers->command_buffers()[0].get();
  vk::Queue queue = device->getQueue(queue_family_index, 0);
  DrawOffscreenFrame(*device, queue, command_buffer, fence.get());
  device->waitIdle();
  Readback(*device, *(vkp_image_view.image_memory()));
}

// Test to verify that destruction of vkp::Device container doesn't affect
// the shared_ptr<vk::Device> ivar within provided a ref ct is maintained.
TEST(VkPrimer, DisposableVKPContainer) {
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
