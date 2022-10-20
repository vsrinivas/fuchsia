// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/zx/vmar.h>

#include <gtest/gtest.h>

#include "src/graphics/examples/vkproto/common/command_buffers.h"
#include "src/graphics/examples/vkproto/common/debug_utils_messenger.h"
#include "src/graphics/examples/vkproto/common/graphics_pipeline.h"
#include "src/graphics/examples/vkproto/common/image_view.h"
#include "src/graphics/examples/vkproto/common/instance.h"
#include "src/graphics/examples/vkproto/common/physical_device.h"
#include "src/graphics/examples/vkproto/common/readback.h"
#include "src/graphics/examples/vkproto/common/render_pass.h"
#include "src/graphics/examples/vkproto/common/swapchain.h"
#include "src/graphics/examples/vkproto/common/utils.h"

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
  EXPECT_EQ(vk::Result::eSuccess,
            device.waitForFences(1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()));
  EXPECT_EQ(vk::Result::eSuccess, device.resetFences(1, &fence));

  EXPECT_EQ(vk::Result::eSuccess, queue.submit(1, &submit_info, fence))
      << "Failed to offscreen submit command buffer.\n";
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
  for (uint32_t i = 0; i < 5; i++) {
    // Try to ensure that the Vulkan device isn't lost. Check multiple times
    // because sometimes it can take a little while to propagate the failure.
    EXPECT_EQ(vk::Result::eSuccess, device->waitIdle());
  }

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

// Test that rendering doesn't fail when a lot of virtual address space is allocated.
TEST(VkProtoDriverTest, LargeVAAllocation) {
  // Size of a single VMAR to be allocated
  constexpr size_t kVmarSize = 1024 * 1024;
  // Size of the total number of VMARs to be allocated. The current value should
  // be enough to fill up most VAs below 4GB, pushing some allocations
  // higher and potentially causing conflicts in the Mali driver.
  constexpr size_t kAllocatedVaSize = 4ul * 1024 * 1024;

  std::vector<zx::vmar> vmars;
  auto defer_destroy = fit::defer([&vmars]() {
    for (auto& vmar : vmars) {
      vmar.destroy();
    }
    vmars.clear();
  });
  size_t total_size_allocated = 0;
  while (total_size_allocated < kAllocatedVaSize) {
    zx::vmar vmar;
    uintptr_t child_addr;
    EXPECT_EQ(ZX_OK, zx::vmar::root_self()->allocate(ZX_VM_CAN_MAP_READ, 0, kVmarSize, &vmar,
                                                     &child_addr));
    total_size_allocated += kVmarSize;
    vmars.push_back(std::move(vmar));
  }

  constexpr uint32_t kIterations = 10;
  for (size_t i = 0; i < kIterations; i++) {
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
}
