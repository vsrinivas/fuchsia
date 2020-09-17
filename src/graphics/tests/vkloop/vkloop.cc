// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/gpu/magma/llcpp/fidl.h>
#include <lib/fdio/unsafe.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include <gtest/gtest.h>

#include "helper/test_device_helper.h"
#include "magma_common_defs.h"
#include "src/graphics/tests/common/utils.h"
#include "src/graphics/tests/common/vulkan_context.h"

#include <vulkan/vulkan.hpp>

namespace {

class VkLoopTest {
 public:
  explicit VkLoopTest(bool hang_on_event) : hang_on_event_(hang_on_event) {}

  bool Initialize();
  bool Exec(bool kill_driver, zx_handle_t magma_device_channel = ZX_HANDLE_INVALID);

  uint32_t get_vendor_id() { return ctx_->physical_device().getProperties().vendorID; }

 private:
  bool InitBuffer();
  bool InitCommandBuffer();

  bool hang_on_event_;
  bool is_initialized_ = false;
  std::unique_ptr<VulkanContext> ctx_;
  VkDescriptorSet vk_descriptor_set_;
  vk::UniqueShaderModule compute_shader_module_;
  vk::UniqueDescriptorPool descriptor_pool_;
  vk::UniqueDescriptorSetLayout descriptor_set_layout_;
  vk::UniquePipelineLayout vk_pipeline_layout_;
  vk::UniquePipeline vk_compute_pipeline_;
  vk::UniqueEvent vk_event_;
  vk::UniqueBuffer buffer_;
  vk::UniqueDeviceMemory buffer_memory_;
  vk::UniqueCommandPool command_pool_;
  std::vector<vk::UniqueCommandBuffer> command_buffers_;
};

bool VkLoopTest::Initialize() {
  if (is_initialized_) {
    return false;
  }

  ctx_ = VulkanContext::Builder{}
             .set_queue_flag_bits(vk::QueueFlagBits::eCompute)
             .set_validation_errors_ignored(true)
             .Unique();
  if (!ctx_) {
    RTN_MSG(false, "Failed to initialize Vulkan.\n");
  }

  if (!InitBuffer()) {
    RTN_MSG(false, "Failed to init buffer.\n");
  }

  if (!InitCommandBuffer()) {
    RTN_MSG(false, "Failed to init command buffer.\n");
  }

  is_initialized_ = true;

  return true;
}

bool VkLoopTest::InitBuffer() {
  const auto &device = ctx_->device();

  // Create buffer.
  constexpr size_t kBufferSize = 4096;
  vk::BufferCreateInfo buffer_info;
  buffer_info.size = kBufferSize;
  buffer_info.usage = vk::BufferUsageFlagBits::eStorageBuffer;
  buffer_info.sharingMode = vk::SharingMode::eExclusive;

  auto rvt_buffer = device->createBufferUnique(buffer_info);
  if (vk::Result::eSuccess != rvt_buffer.result) {
    RTN_MSG(false, "VK Error: 0x%x - Create buffer.\n", rvt_buffer.result);
  }
  buffer_ = std::move(rvt_buffer.value);

  // Find host visible buffer memory type.
  vk::PhysicalDeviceMemoryProperties memory_props;
  ctx_->physical_device().getMemoryProperties(&memory_props);

  uint32_t memory_type = 0;
  for (; memory_type < memory_props.memoryTypeCount; memory_type++) {
    if (memory_props.memoryTypes[memory_type].propertyFlags &
        vk::MemoryPropertyFlagBits::eHostVisible) {
      break;
    }
  }
  if (memory_type >= memory_props.memoryTypeCount) {
    RTN_MSG(false, "Can't find host visible memory for buffer.\n");
  }

  // Allocate buffer memory.
  vk::MemoryRequirements buffer_memory_reqs = device->getBufferMemoryRequirements(*buffer_);
  vk::MemoryAllocateInfo alloc_info;
  alloc_info.allocationSize = buffer_memory_reqs.size;
  alloc_info.memoryTypeIndex = memory_type;

  auto rvt_memory = device->allocateMemoryUnique(alloc_info);
  if (vk::Result::eSuccess != rvt_memory.result) {
    RTN_MSG(false, "VK Error: 0x%x - Create buffer memory.\n", rvt_memory.result);
  }
  buffer_memory_ = std::move(rvt_memory.value);

  // Map, set, flush and bind buffer memory.
  void *addr;
  auto rv_map =
      device->mapMemory(*buffer_memory_, 0 /* offset */, kBufferSize, vk::MemoryMapFlags(), &addr);
  if (vk::Result::eSuccess != rv_map) {
    RTN_MSG(false, "VK Error: 0x%x - Map buffer memory.\n", rv_map);
  }

  // Set to 1 so the shader will ping pong about zero.
  *reinterpret_cast<uint32_t *>(addr) = 1;

  vk::MappedMemoryRange memory_range;
  memory_range.memory = *buffer_memory_;
  memory_range.size = VK_WHOLE_SIZE;

  auto rv_flush = device->flushMappedMemoryRanges(1, &memory_range);
  if (vk::Result::eSuccess != rv_flush) {
    RTN_MSG(false, "VK Error: 0x%x - Flush buffer memory range.\n", rv_flush);
  }

  auto rv_bind = device->bindBufferMemory(*buffer_, *buffer_memory_, 0 /* offset */);
  if (vk::Result::eSuccess != rv_bind) {
    RTN_MSG(false, "VK Error: 0x%x - Bind buffer memory.\n", rv_bind);
  }

  return true;
}

bool VkLoopTest::InitCommandBuffer() {
  const auto &device = *ctx_->device();
  vk::CommandPoolCreateInfo command_pool_info;
  command_pool_info.queueFamilyIndex = ctx_->queue_family_index();

  auto rvt_command_pool = ctx_->device()->createCommandPoolUnique(command_pool_info);
  if (vk::Result::eSuccess != rvt_command_pool.result) {
    RTN_MSG(false, "VK Error: 0x%x - Create command pool.\n", rvt_command_pool.result);
  }
  command_pool_ = std::move(rvt_command_pool.value);

  vk::CommandBufferAllocateInfo cmd_buff_alloc_info;
  cmd_buff_alloc_info.commandPool = *command_pool_;
  cmd_buff_alloc_info.level = vk::CommandBufferLevel::ePrimary;
  cmd_buff_alloc_info.commandBufferCount = 1;

  auto rvt_alloc_cmd_bufs = ctx_->device()->allocateCommandBuffersUnique(cmd_buff_alloc_info);
  if (vk::Result::eSuccess != rvt_alloc_cmd_bufs.result) {
    RTN_MSG(false, "VK Error: 0x%x - Allocate command buffers.\n", rvt_alloc_cmd_bufs.result);
  }
  command_buffers_ = std::move(rvt_alloc_cmd_bufs.value);
  vk::UniqueCommandBuffer &command_buffer = command_buffers_.front();

  auto rv_begin = command_buffer->begin(vk::CommandBufferBeginInfo{});
  if (vk::Result::eSuccess != rv_begin) {
    RTN_MSG(false, "VK Error: 0x%x - Begin command buffer.\n", rv_begin);
  }

  vk::ShaderModuleCreateInfo sh_info;
  std::vector<uint8_t> shader;
  {
    int fd = open("/pkg/data/vkloop.spv", O_RDONLY);
    if (fd < 0) {
      RTN_MSG(false, "Couldn't open shader binary: %d\n", fd);
    }

    struct stat buf;
    fstat(fd, &buf);
    shader.resize(buf.st_size);
    read(fd, shader.data(), shader.size());
    close(fd);

    sh_info.codeSize = shader.size();
    sh_info.pCode = reinterpret_cast<uint32_t *>(shader.data());
  }

  auto csm = ctx_->device()->createShaderModuleUnique(sh_info);
  if (vk::Result::eSuccess != csm.result) {
    RTN_MSG(false, "vkCreateShaderModule failed: %d\n", csm.result);
  }
  compute_shader_module_ = std::move(csm.value);

  vk::DescriptorSetLayoutBinding descriptor_set_layout_binding;
  descriptor_set_layout_binding.setBinding(0)
      .setDescriptorCount(1)
      .setDescriptorType(vk::DescriptorType::eStorageBuffer)
      .setStageFlags(vk::ShaderStageFlagBits::eCompute);

  auto descriptor_set_layout_create_info =
      vk::DescriptorSetLayoutCreateInfo().setBindingCount(1).setPBindings(
          &descriptor_set_layout_binding);

  auto dsl = ctx_->device()->createDescriptorSetLayoutUnique(descriptor_set_layout_create_info);
  if (vk::Result::eSuccess != dsl.result) {
    RTN_MSG(false, "vkCreateDescriptorSetLayout failed: %d\n", dsl.result);
  }
  descriptor_set_layout_ = std::move(dsl.value);

  auto pool_size =
      vk::DescriptorPoolSize().setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1);

  auto descriptor_pool_create_info =
      vk::DescriptorPoolCreateInfo().setMaxSets(1).setPoolSizeCount(1).setPPoolSizes(&pool_size);

  auto dp = ctx_->device()->createDescriptorPoolUnique(descriptor_pool_create_info);
  if (vk::Result::eSuccess != dp.result) {
    RTN_MSG(false, "vkCreateDescriptorPool failed: %d\n", dp.result);
  }
  descriptor_pool_ = std::move(dp.value);

  VkDescriptorSetLayout dslp = descriptor_set_layout_.get();
  VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = nullptr,
      .descriptorPool = *descriptor_pool_,
      .descriptorSetCount = 1,
      .pSetLayouts = &dslp,
  };

  VkResult result;
  if ((result = vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                         &vk_descriptor_set_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkAllocateDescriptorSets failed: %d\n", result);
  }

  VkDescriptorBufferInfo descriptor_buffer_info = {
      .buffer = *buffer_, .offset = 0, .range = VK_WHOLE_SIZE};

  VkWriteDescriptorSet write_descriptor_set = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                               .pNext = nullptr,
                                               .dstSet = vk_descriptor_set_,
                                               .dstBinding = 0,
                                               .dstArrayElement = 0,
                                               .descriptorCount = 1,
                                               .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                               .pImageInfo = nullptr,
                                               .pBufferInfo = &descriptor_buffer_info,
                                               .pTexelBufferView = nullptr};
  vkUpdateDescriptorSets(device,
                         1,  // descriptorWriteCount
                         &write_descriptor_set,
                         0,         // descriptorCopyCount
                         nullptr);  // pDescriptorCopies

  vk::PipelineLayoutCreateInfo pipeline_create_info;
  pipeline_create_info.setLayoutCount = 1;
  pipeline_create_info.pSetLayouts = &*descriptor_set_layout_;

  auto pipeline_layout = ctx_->device()->createPipelineLayoutUnique(pipeline_create_info);
  if (vk::Result::eSuccess != pipeline_layout.result) {
    RTN_MSG(false, "vkCreatePipelineLayout failed: %d\n", result);
  }
  vk_pipeline_layout_ = std::move(pipeline_layout.value);

  vk::ComputePipelineCreateInfo pipeline_info;
  pipeline_info.stage = vk::PipelineShaderStageCreateInfo{
      {}, vk::ShaderStageFlagBits::eCompute, *compute_shader_module_, "main", nullptr};
  pipeline_info.layout = *vk_pipeline_layout_;

  auto compute_pipeline = ctx_->device()->createComputePipelineUnique(nullptr, pipeline_info);
  if (vk::Result::eSuccess != compute_pipeline.result) {
    RTN_MSG(false, "vkCreateComputePipelines failed: %d\n", compute_pipeline.result);
  }
  vk_compute_pipeline_ = std::move(compute_pipeline.value);

  if (hang_on_event_) {
    auto evt = ctx_->device()->createEventUnique(vk::EventCreateInfo());
    if (vk::Result::eSuccess != evt.result) {
      RTN_MSG(false, "VK Error: 0x%x - Create event.\n", evt.result);
    }
    vk_event_ = std::move(evt.value);

    command_buffer->waitEvents(1, &vk_event_.get(), vk::PipelineStageFlagBits::eHost,
                               vk::PipelineStageFlagBits::eTransfer, 0, nullptr, 0, nullptr, 0,
                               nullptr);
  } else {
    vkCmdBindPipeline(*command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, *vk_compute_pipeline_);

    vkCmdBindDescriptorSets(*command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, *vk_pipeline_layout_,
                            0,  // firstSet
                            1,  // descriptorSetCount,
                            &vk_descriptor_set_,
                            0,         // dynamicOffsetCount
                            nullptr);  // pDynamicOffsets

    vkCmdDispatch(*command_buffer, 1, 1, 1);
  }

  auto rv_end = command_buffer->end();
  if (vk::Result::eSuccess != rv_end) {
    RTN_MSG(false, "VK Error: 0x%x - End command buffer.\n", rv_end);
  }

  return true;
}

bool VkLoopTest::Exec(bool kill_driver, zx_handle_t magma_device_channel) {
  auto rv_wait = ctx_->queue().waitIdle();
  if (vk::Result::eSuccess != rv_wait) {
    RTN_MSG(false, "VK Error: 0x%x - Queue wait idle.\n", rv_wait);
  }

  // Submit command buffer and wait for it to complete.
  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = static_cast<uint32_t>(command_buffers_.size());
  const vk::CommandBuffer &command_buffer = command_buffers_.front().get();
  submit_info.pCommandBuffers = &command_buffer;

  auto rv = ctx_->queue().submit(1 /* submitCt */, &submit_info, nullptr /* fence */);
  if (rv != vk::Result::eSuccess) {
    RTN_MSG(false, "VK Error: 0x%x - vk::Queue submit failed.\n", rv);
  }

  if (kill_driver) {
    // TODO: Unbind and rebind driver once that supports forcibly tearing down client connections.
    auto result = llcpp::fuchsia::gpu::magma::Device::Call::TestRestart(
        zx::unowned_channel(magma_device_channel));
    EXPECT_EQ(ZX_OK, result.status());
  }

  constexpr int kReps = 5;
  for (int i = 0; i < kReps; i++) {
    rv_wait = ctx_->queue().waitIdle();
    if (vk::Result::eSuccess != rv_wait) {
      break;
    }
  }
  if (vk::Result::eErrorDeviceLost != rv_wait) {
    RTN_MSG(false, "VK Error: Result was 0x%x instead of vk::Result::eErrorDeviceLost\n", rv_wait);
  }

  return true;
}

TEST(VkLoop, InfiniteLoop) {
  for (int i = 0; i < 2; i++) {
    VkLoopTest test(false);
    ASSERT_TRUE(test.Initialize());
    ASSERT_TRUE(test.Exec(false));
  }
}

TEST(VkLoop, EventHang) {
  VkLoopTest test(true);
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(false));
}

TEST(VkLoop, DriverDeath) {
  VkLoopTest test(true);
  ASSERT_TRUE(test.Initialize());

  magma::TestDeviceBase test_device(test.get_vendor_id());
  uint64_t is_supported = 0;
  magma_status_t status =
      magma_query2(test_device.device(), MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED, &is_supported);
  if (status != MAGMA_STATUS_OK || !is_supported) {
    fprintf(stderr, "Test restart not supported: status %d is_supported %lu\n", status,
            is_supported);
    GTEST_SKIP();
  }
  ASSERT_TRUE(test.Exec(true, test_device.channel()->get()));
}

}  // namespace
