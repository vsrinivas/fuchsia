// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "gtest/gtest.h"
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
  bool Exec(bool kill_driver);

 private:
  bool InitBuffer();
  bool InitCommandBuffer();

  bool hang_on_event_;
  bool is_initialized_ = false;
  std::unique_ptr<VulkanContext> ctx_;
  VkDescriptorSet vk_descriptor_set_;
  VkPipelineLayout vk_pipeline_layout_;
  VkPipeline vk_compute_pipeline_;
  VkEvent vk_event_;

  VkCommandPool vk_command_pool_;
  VkCommandBuffer vk_command_buffer_;

  VkBuffer vk_buffer_;
  VkDeviceMemory device_memory_;
};

bool VkLoopTest::Initialize() {
  if (is_initialized_) {
    return false;
  }

  ctx_ = VulkanContext::Builder{}.set_queue_flag_bits(vk::QueueFlagBits::eCompute).Unique();
  if (!ctx_) {
    RTN_MSG(false, "Failed to initialize Vulkan.\n");
  }

  if (!InitBuffer()) {
    RTN_MSG(false, "Failed to init buffer.\n");
  }

  if (!InitCommandBuffer()) {
    RTN_MSG(false, "InitImage failed.\n");
  }

  is_initialized_ = true;

  return true;
}

bool VkLoopTest::InitBuffer() {
  VkResult result;
  const auto &device = *ctx_->device();

  constexpr size_t kBufferSize = 4096;
  VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .size = kBufferSize,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,     // ignored
      .pQueueFamilyIndices = nullptr  // ignored
  };

  if ((result = vkCreateBuffer(device, &buffer_create_info, nullptr, &vk_buffer_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkCreateBuffer failed: %d\n", result);
  }

  VkMemoryRequirements buffer_memory_reqs = {};
  vkGetBufferMemoryRequirements(device, vk_buffer_, &buffer_memory_reqs);

  VkPhysicalDeviceMemoryProperties memory_props;
  vkGetPhysicalDeviceMemoryProperties(ctx_->physical_device(), &memory_props);

  device_memory_ = VK_NULL_HANDLE;

  for (uint32_t i = 0; i < memory_props.memoryTypeCount; i++) {
    if (memory_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      VkMemoryAllocateInfo allocate_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                            .pNext = nullptr,
                                            .allocationSize = buffer_memory_reqs.size,
                                            .memoryTypeIndex = i};
      if ((result = vkAllocateMemory(device, &allocate_info, nullptr, &device_memory_)) !=
          VK_SUCCESS) {
        RTN_MSG(false, "vkAllocateMemory failed: %d\n", result);
      }
      break;
    }
  }

  if (device_memory_ == VK_NULL_HANDLE) {
    RTN_MSG(false, "Couldn't find host visible memory.\n");
  }

  {
    void *data;
    if ((result = vkMapMemory(device, device_memory_,
                              0,  // offset
                              VK_WHOLE_SIZE,
                              0,  // flags
                              &data)) != VK_SUCCESS) {
      RTN_MSG(false, "vkMapMemory failed: %d\n", result);
    }
    // Set to 1 so the shader will ping pong about zero
    *reinterpret_cast<uint32_t *>(data) = 1;

    VkMappedMemoryRange memory_range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                        .pNext = nullptr,
                                        .memory = device_memory_,
                                        .offset = 0,
                                        .size = VK_WHOLE_SIZE};
    if ((result = vkFlushMappedMemoryRanges(device, 1, &memory_range)) != VK_SUCCESS) {
      RTN_MSG(false, "vkFlushMappedMemoryRanges failed: %d\n", result);
    }
  }

  if ((result = vkBindBufferMemory(device, vk_buffer_, device_memory_,
                                   0  // memoryOffset
                                   )) != VK_SUCCESS) {
    RTN_MSG(false, "vkBindBufferMemory failed: %d\n", result);
  }

  return true;
}

bool VkLoopTest::InitCommandBuffer() {
  const auto &device = *ctx_->device();
  VkCommandPoolCreateInfo command_pool_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueFamilyIndex = 0,
  };
  VkResult result;
  if ((result = vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                                    &vk_command_pool_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkCreateCommandPool failed: %d\n", result);
  }

  VkCommandBufferAllocateInfo command_buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = vk_command_pool_,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  if ((result = vkAllocateCommandBuffers(device, &command_buffer_create_info,
                                         &vk_command_buffer_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkAllocateCommandBuffers failed: %d\n", result);
  }

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = 0,
      .pInheritanceInfo = nullptr,  // ignored for primary buffers
  };
  if ((result = vkBeginCommandBuffer(vk_command_buffer_, &begin_info)) != VK_SUCCESS) {
    RTN_MSG(false, "vkBeginCommandBuffer failed: %d\n", result);
  }

  VkShaderModule compute_shader_module_;
  VkShaderModuleCreateInfo sh_info = {};
  sh_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

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

  if ((result = vkCreateShaderModule(device, &sh_info, NULL, &compute_shader_module_)) !=
      VK_SUCCESS) {
    RTN_MSG(false, "vkCreateShaderModule failed: %d\n", result);
  }

  VkDescriptorSetLayoutBinding descriptor_set_layout_bindings = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .pImmutableSamplers = nullptr};

  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .bindingCount = 1,
      .pBindings = &descriptor_set_layout_bindings,
  };

  VkDescriptorSetLayout descriptor_set_layout;

  if ((result = vkCreateDescriptorSetLayout(device, &descriptor_set_layout_create_info, nullptr,
                                            &descriptor_set_layout)) != VK_SUCCESS) {
    RTN_MSG(false, "vkCreateDescriptorSetLayout failed: %d\n", result);
  }

  VkDescriptorPoolSize pool_size = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    .descriptorCount = 1};

  VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &pool_size};

  VkDescriptorPool descriptor_pool;
  if ((result = vkCreateDescriptorPool(device, &descriptor_pool_create_info, nullptr,
                                       &descriptor_pool)) != VK_SUCCESS) {
    RTN_MSG(false, "vkCreateDescriptorPool failed: %d\n", result);
  }

  VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = nullptr,
      .descriptorPool = descriptor_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptor_set_layout,
  };

  if ((result = vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                         &vk_descriptor_set_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkAllocateDescriptorSets failed: %d\n", result);
  }

  VkDescriptorBufferInfo descriptor_buffer_info = {
      .buffer = vk_buffer_, .offset = 0, .range = VK_WHOLE_SIZE};

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

  VkPipelineLayoutCreateInfo pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptor_set_layout,
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = nullptr};

  if ((result = vkCreatePipelineLayout(device, &pipeline_create_info, nullptr,
                                       &vk_pipeline_layout_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkCreatePipelineLayout failed: %d\n", result);
  }

  VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = compute_shader_module_,
                .pName = "main",
                .pSpecializationInfo = nullptr},
      .layout = vk_pipeline_layout_,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = 0};

  if ((result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                         &vk_compute_pipeline_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkCreateComputePipelines failed: %d\n", result);
  }

  if (hang_on_event_) {
    VkEventCreateInfo event_info = {
        .sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO, .pNext = nullptr, .flags = 0};
    if ((result = vkCreateEvent(device, &event_info, nullptr, &vk_event_)) != VK_SUCCESS) {
      RTN_MSG(false, "vkCreateEvent failed: %d\n", result);
    }

    vkCmdWaitEvents(vk_command_buffer_, 1, &vk_event_, VK_PIPELINE_STAGE_HOST_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr, 0, nullptr);
  } else {
    vkCmdBindPipeline(vk_command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, vk_compute_pipeline_);

    vkCmdBindDescriptorSets(vk_command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, vk_pipeline_layout_,
                            0,  // firstSet
                            1,  // descriptorSetCount,
                            &vk_descriptor_set_,
                            0,         // dynamicOffsetCount
                            nullptr);  // pDynamicOffsets

    vkCmdDispatch(vk_command_buffer_, 1, 1, 1);
  }

  if ((result = vkEndCommandBuffer(vk_command_buffer_)) != VK_SUCCESS) {
    RTN_MSG(false, "vkEndCommandBuffer failed: %d\n", result);
  }

  return true;
}

bool VkLoopTest::Exec(bool kill_driver) {
  VkResult result;
  const auto &queue = ctx_->queue();
  result = vkQueueWaitIdle(queue);
  if (result != VK_SUCCESS) {
    RTN_MSG(false, "vkQueueWaitIdle failed with result %d\n", result);
  }

  VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = nullptr,
      .waitSemaphoreCount = 0,
      .pWaitSemaphores = nullptr,
      .pWaitDstStageMask = nullptr,
      .commandBufferCount = 1,
      .pCommandBuffers = &vk_command_buffer_,
      .signalSemaphoreCount = 0,
      .pSignalSemaphores = nullptr,
  };

  if ((result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE)) != VK_SUCCESS) {
    RTN_MSG(false, "vkQueueSubmit failed.\n");
  }

  if (kill_driver) {
    magma::TestDeviceBase test_device(ctx_->physical_device().getProperties().vendorID);
    uint64_t is_supported = 0;
    magma_status_t status =
        magma_query2(test_device.device(), MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED, &is_supported);
    if (status != MAGMA_STATUS_OK || !is_supported) {
      RTN_MSG(true, "Test restart not supported: status %d is_supported %lu\n", status,
              is_supported);
    }

    // TODO: Unbind and rebind driver once that supports forcibly tearing down client connections.
    EXPECT_EQ(ZX_OK, fuchsia_gpu_magma_DeviceTestRestart(test_device.channel()->get()));
  }

  constexpr int kReps = 5;
  for (int i = 0; i < kReps; i++) {
    result = vkQueueWaitIdle(queue);
    if (result != VK_SUCCESS) {
      break;
    }
  }
  if (result != VK_ERROR_DEVICE_LOST) {
    RTN_MSG(false, "Result was %d instead of VK_ERROR_DEVICE_LOST.\n", result);
  }

  return true;
}

TEST(Vulkan, InfiniteLoop) {
  for (int i = 0; i < 2; i++) {
    VkLoopTest test(false);
    ASSERT_TRUE(test.Initialize());
    ASSERT_TRUE(test.Exec(false));
  }
}

TEST(Vulkan, EventHang) {
  VkLoopTest test(true);
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(false));
}

TEST(Vulkan, DriverDeath) {
  VkLoopTest test(true);
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec(true));
}

}  // namespace
