// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "hwcpipe.h"
#include "src/graphics/tests/common/vulkan_context.h"

#include <vulkan/vulkan.hpp>

namespace {

#define EXPECT_VK_SUCCESS(value) EXPECT_EQ(vk::Result::eSuccess, (value))

class ProtectedMode : public testing::Test {
 public:
  bool Initialize();

 protected:
  std::unique_ptr<VulkanContext> ctx_;
  bool device_supports_protected_memory_ = false;
  vk::DispatchLoaderDynamic loader_;
};

bool ProtectedMode::Initialize() {
  constexpr size_t kPhysicalDeviceIndex = 0;
  vk::ApplicationInfo app_info;
  app_info.pApplicationName = "vkext";
  app_info.apiVersion = VK_API_VERSION_1_1;
  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;
  ctx_ = std::make_unique<VulkanContext>(kPhysicalDeviceIndex);
  ctx_->set_instance_info(instance_info);
  if (!ctx_->InitInstance()) {
    return false;
  }

  loader_.init(*ctx_->instance(), vkGetInstanceProcAddr);
  if (!ctx_->InitQueueFamily()) {
    return false;
  }

  // Set |device_supports_protected_memory_| flag.
  vk::PhysicalDeviceProtectedMemoryFeatures protected_memory(VK_TRUE);
  vk::PhysicalDeviceProperties physical_device_properties;
  ctx_->physical_device().getProperties(&physical_device_properties);
  if (VK_VERSION_MAJOR(physical_device_properties.apiVersion) != 1 ||
      VK_VERSION_MINOR(physical_device_properties.apiVersion) > 0) {
    vk::PhysicalDeviceFeatures2 features2;
    features2.pNext = &protected_memory;
    ctx_->physical_device().getFeatures2(&features2);
    if (protected_memory.protectedMemory) {
      device_supports_protected_memory_ = true;
    }
  }

  vk::DeviceCreateInfo device_info;
  device_info.pNext = &protected_memory;
  vk::DeviceQueueCreateInfo queue_info = ctx_->queue_info();
  queue_info.flags = vk::DeviceQueueCreateFlagBits::eProtected;
  device_info.setQueueCreateInfos(queue_info);

  ctx_->set_device_info(device_info);
  if (!ctx_->InitDevice()) {
    return false;
  }
  return true;
}

struct BufferData {
  vk::UniqueBuffer buffer;
  vk::UniqueDeviceMemory device_memory;
};

BufferData CreateProtectedBuffer(vk::Device device, size_t size) {
  BufferData buffer_data;
  auto [buffer_res, buffer] =
      device.createBufferUnique(vk::BufferCreateInfo()
                                    .setSize(size)
                                    .setUsage(vk::BufferUsageFlagBits::eTransferDst)
                                    .setFlags(vk::BufferCreateFlagBits::eProtected));
  EXPECT_EQ(buffer_res, vk::Result::eSuccess);
  auto buffer_requirements = device.getBufferMemoryRequirements(*buffer);
  uint32_t memory_type = __builtin_ctz(buffer_requirements.memoryTypeBits);
  auto [memory_res, memory] =
      device.allocateMemoryUnique(vk::MemoryAllocateInfo()
                                      .setAllocationSize(buffer_requirements.size)
                                      .setMemoryTypeIndex(memory_type));
  EXPECT_EQ(memory_res, vk::Result::eSuccess);
  EXPECT_VK_SUCCESS(device.bindBufferMemory(*buffer, *memory, 0));
  buffer_data.buffer = std::move(buffer);
  buffer_data.device_memory = std::move(memory);
  return buffer_data;
}

// Check that HWCPipe doesn't hang even if the GPU is in protected mode.
TEST_F(ProtectedMode, PerformanceCounters) {
  ASSERT_TRUE(Initialize());
  if (!device_supports_protected_memory_) {
    GTEST_SKIP();
  }
  auto [pool_res, command_pool] = ctx_->device()->createCommandPoolUnique(
      vk::CommandPoolCreateInfo()
          .setFlags(vk::CommandPoolCreateFlagBits::eProtected)
          .setQueueFamilyIndex(ctx_->queue_family_index()));
  EXPECT_EQ(vk::Result::eSuccess, pool_res);

  std::vector<vk::UniqueCommandBuffer> command_buffers;
  {
    auto info = vk::CommandBufferAllocateInfo()
                    .setCommandPool(*command_pool)
                    .setLevel(vk::CommandBufferLevel::ePrimary)
                    .setCommandBufferCount(1);
    auto result = ctx_->device()->allocateCommandBuffersUnique(info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    command_buffers = std::move(result.value);
  }

  EXPECT_EQ(vk::Result::eSuccess, command_buffers[0]->begin(vk::CommandBufferBeginInfo()));

  constexpr uint32_t kBufferSize = 1024;
  auto buffer_data = CreateProtectedBuffer(*ctx_->device(), kBufferSize);
  command_buffers[0]->fillBuffer(*buffer_data.buffer, 0, kBufferSize, 1);
  EXPECT_EQ(vk::Result::eSuccess, command_buffers[0]->end());

  // A protected submit should switch the GPU into protected mode.
  vk::StructureChain<vk::SubmitInfo, vk::ProtectedSubmitInfo> submit_info;
  submit_info.get<vk::SubmitInfo>().setCommandBuffers(*command_buffers[0]);
  submit_info.get<vk::ProtectedSubmitInfo>().setProtectedSubmit(true);
  EXPECT_EQ(vk::Result::eSuccess, ctx_->queue().submit(submit_info.get<vk::SubmitInfo>()));

  EXPECT_VK_SUCCESS(ctx_->queue().waitIdle());
  hwcpipe::HWCPipe pipe;
  pipe.set_enabled_gpu_counters(pipe.gpu_profiler()->supported_counters());
  // HWCPipe.run will start running and sample the performance counters initially.
  pipe.run();
}

}  // namespace
