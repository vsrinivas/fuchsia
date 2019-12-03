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

#include <vulkan/vulkan.h>

#include "gtest/gtest.h"
#include "helper/test_device_helper.h"
#include "magma_common_defs.h"

#define PRINT_STDERR(format, ...) \
  fprintf(stderr, "%s:%d " format "\n", __FILE__, __LINE__, ##__VA_ARGS__)

namespace {

class VkLoopTest {
 public:
  explicit VkLoopTest(bool hang_on_event) : hang_on_event_(hang_on_event) {}

  bool Initialize();
  bool Exec(bool kill_driver);

 private:
  bool InitVulkan();
  bool InitBuffer();
  bool InitCommandBuffer();

  bool hang_on_event_;
  bool is_initialized_ = false;
  VkPhysicalDevice vk_physical_device_;
  VkDevice vk_device_;
  VkQueue vk_queue_;
  uint32_t vendor_id_ = 0;

  VkCommandPool vk_command_pool_;
  VkCommandBuffer vk_command_buffer_;

  VkBuffer vk_buffer_;
  VkDeviceMemory device_memory_;
};

bool VkLoopTest::Initialize() {
  if (is_initialized_)
    return false;

  if (!InitVulkan()) {
    PRINT_STDERR("failed to initialize Vulkan");
    return false;
  }

  if (!InitBuffer()) {
    PRINT_STDERR("failed to init buffer");
    return false;
  }

  if (!InitCommandBuffer()) {
    PRINT_STDERR("InitImage failed");
    return false;
  }

  is_initialized_ = true;

  return true;
}

bool VkLoopTest::InitVulkan() {
  VkInstanceCreateInfo create_info{
      VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,  // VkStructureType             sType;
      nullptr,                                 // const void*                 pNext;
      0,                                       // VkInstanceCreateFlags       flags;
      nullptr,                                 // const VkApplicationInfo*    pApplicationInfo;
      0,                                       // uint32_t                    enabledLayerCount;
      nullptr,                                 // const char* const*          ppEnabledLayerNames;
      0,                                       // uint32_t                    enabledExtensionCount;
      nullptr,  // const char* const*          ppEnabledExtensionNames;
  };
  VkAllocationCallbacks* allocation_callbacks = nullptr;
  VkInstance instance;
  VkResult result;

  if ((result = vkCreateInstance(&create_info, allocation_callbacks, &instance)) != VK_SUCCESS) {
    PRINT_STDERR("vkCreateInstance failed %d", result);
    return false;
  }

  uint32_t physical_device_count;
  if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr)) !=
      VK_SUCCESS) {
    PRINT_STDERR("vkEnumeratePhysicalDevices failed %d", result);
    return false;
  }

  if (physical_device_count < 1) {
    PRINT_STDERR("unexpected physical_device_count %d", physical_device_count);
    return false;
  }

  std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
  if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                           physical_devices.data())) != VK_SUCCESS) {
    PRINT_STDERR("vkEnumeratePhysicalDevices failed %d", result);
    return false;
  }

  VkPhysicalDeviceProperties device_properties;
  vkGetPhysicalDeviceProperties(physical_devices[0], &device_properties);

  vendor_id_ = device_properties.vendorID;

  uint32_t queue_family_count;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count, nullptr);

  if (queue_family_count < 1) {
    PRINT_STDERR("invalid queue_family_count %d", queue_family_count);
    return false;
  }

  std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count,
                                           queue_family_properties.data());

  int32_t queue_family_index = -1;
  for (uint32_t i = 0; i < queue_family_count; i++) {
    if (queue_family_properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      queue_family_index = i;
      break;
    }
  }

  if (queue_family_index < 0) {
    PRINT_STDERR("couldn't find an appropriate queue");
    return false;
  }

  float queue_priorities[1] = {0.0};

  VkDeviceQueueCreateInfo queue_create_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .queueFamilyIndex = 0,
                                               .queueCount = 1,
                                               .pQueuePriorities = queue_priorities};

  std::vector<const char*> enabled_extension_names;

  VkDeviceCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = static_cast<uint32_t>(enabled_extension_names.size()),
      .ppEnabledExtensionNames = enabled_extension_names.data(),
      .pEnabledFeatures = nullptr};
  VkDevice vkdevice;

  if ((result = vkCreateDevice(physical_devices[0], &createInfo, nullptr /* allocationcallbacks */,
                               &vkdevice)) != VK_SUCCESS) {
    PRINT_STDERR("vkCreateDevice failed: %d", result);
    return false;
  }

  vk_physical_device_ = physical_devices[0];
  vk_device_ = vkdevice;

  vkGetDeviceQueue(vkdevice, queue_family_index, 0, &vk_queue_);

  return true;
}

bool VkLoopTest::InitBuffer() {
  VkResult result;

  VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .size = 4096,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,     // ignored
      .pQueueFamilyIndices = nullptr  // ignored
  };

  if ((result = vkCreateBuffer(vk_device_, &buffer_create_info, nullptr, &vk_buffer_)) !=
      VK_SUCCESS) {
    PRINT_STDERR("vkCreateBuffer failed: %d", result);
    return false;
  }

  VkMemoryRequirements buffer_memory_reqs = {};
  vkGetBufferMemoryRequirements(vk_device_, vk_buffer_, &buffer_memory_reqs);

  VkPhysicalDeviceMemoryProperties memory_props;
  vkGetPhysicalDeviceMemoryProperties(vk_physical_device_, &memory_props);

  device_memory_ = VK_NULL_HANDLE;

  for (uint32_t i = 0; i < memory_props.memoryTypeCount; i++) {
    if (memory_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      VkMemoryAllocateInfo allocate_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                            .pNext = nullptr,
                                            .allocationSize = buffer_memory_reqs.size,
                                            .memoryTypeIndex = i};

      if ((result = vkAllocateMemory(vk_device_, &allocate_info, nullptr, &device_memory_)) !=
          VK_SUCCESS) {
        PRINT_STDERR("vkAllocateMemory failed: %d", result);
        return false;
      }
      break;
    }
  }

  if (device_memory_ == VK_NULL_HANDLE) {
    PRINT_STDERR("Couldn't find host visible memory");
    return false;
  }

  {
    void* data;
    if ((result = vkMapMemory(vk_device_, device_memory_,
                              0,  // offset
                              VK_WHOLE_SIZE,
                              0,  // flags
                              &data)) != VK_SUCCESS) {
      PRINT_STDERR("vkMapMemory failed: %d", result);
      return false;
    }
    // Set to 1 so the shader will ping pong about zero
    *reinterpret_cast<uint32_t*>(data) = 1;

    VkMappedMemoryRange memory_range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                        .pNext = nullptr,
                                        .memory = device_memory_,
                                        .offset = 0,
                                        .size = VK_WHOLE_SIZE};
    if ((result = vkFlushMappedMemoryRanges(vk_device_, 1, &memory_range)) != VK_SUCCESS) {
      PRINT_STDERR("vkFlushMappedMemoryRanges failed: %d", result);
      return false;
    }
  }

  if ((result = vkBindBufferMemory(vk_device_, vk_buffer_, device_memory_,
                                   0  // memoryOffset
                                   )) != VK_SUCCESS) {
    PRINT_STDERR("vkBindBufferMemory failed: %d", result);
    return false;
  }

  return true;
}

bool VkLoopTest::InitCommandBuffer() {
  VkCommandPoolCreateInfo command_pool_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueFamilyIndex = 0,
  };
  VkResult result;
  if ((result = vkCreateCommandPool(vk_device_, &command_pool_create_info, nullptr,
                                    &vk_command_pool_)) != VK_SUCCESS) {
    PRINT_STDERR("vkCreateCommandPool failed: %d", result);
    return false;
  }

  VkCommandBufferAllocateInfo command_buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = vk_command_pool_,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  if ((result = vkAllocateCommandBuffers(vk_device_, &command_buffer_create_info,
                                         &vk_command_buffer_)) != VK_SUCCESS) {
    PRINT_STDERR("vkAllocateCommandBuffers failed: %d", result);
    return false;
  }

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = 0,
      .pInheritanceInfo = nullptr,  // ignored for primary buffers
  };
  if ((result = vkBeginCommandBuffer(vk_command_buffer_, &begin_info)) != VK_SUCCESS) {
    PRINT_STDERR("vkBeginCommandBuffer failed: %d", result);
    return false;
  }

  VkShaderModule compute_shader_module_;
  VkShaderModuleCreateInfo sh_info = {};
  sh_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

  std::vector<uint8_t> shader;
  {
    int fd = open("/pkg/data/vkloop.spv", O_RDONLY);
    if (fd < 0) {
      PRINT_STDERR("couldn't open shader binary: %d", fd);
      return false;
    }

    struct stat buf;
    fstat(fd, &buf);
    shader.resize(buf.st_size);
    read(fd, shader.data(), shader.size());
    close(fd);

    sh_info.codeSize = shader.size();
    sh_info.pCode = reinterpret_cast<uint32_t*>(shader.data());
  }

  if ((result = vkCreateShaderModule(vk_device_, &sh_info, NULL, &compute_shader_module_)) !=
      VK_SUCCESS) {
    PRINT_STDERR("vkCreateShaderModule failed: %d", result);
    return false;
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

  if ((result = vkCreateDescriptorSetLayout(vk_device_, &descriptor_set_layout_create_info, nullptr,
                                            &descriptor_set_layout)) != VK_SUCCESS) {
    PRINT_STDERR("vkCreateDescriptorSetLayout failed: %d", result);
    return false;
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
  if ((result = vkCreateDescriptorPool(vk_device_, &descriptor_pool_create_info, nullptr,
                                       &descriptor_pool)) != VK_SUCCESS) {
    PRINT_STDERR("vkCreateDescriptorPool failed: %d", result);
    return false;
  }

  VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = nullptr,
      .descriptorPool = descriptor_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptor_set_layout,
  };

  VkDescriptorSet descriptor_set;

  if ((result = vkAllocateDescriptorSets(vk_device_, &descriptor_set_allocate_info,
                                         &descriptor_set)) != VK_SUCCESS) {
    PRINT_STDERR("vkAllocateDescriptorSets failed: %d", result);
    return false;
  }

  VkDescriptorBufferInfo descriptor_buffer_info = {
      .buffer = vk_buffer_, .offset = 0, .range = VK_WHOLE_SIZE};

  VkWriteDescriptorSet write_descriptor_set = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                               .pNext = nullptr,
                                               .dstSet = descriptor_set,
                                               .dstBinding = 0,
                                               .dstArrayElement = 0,
                                               .descriptorCount = 1,
                                               .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                               .pImageInfo = nullptr,
                                               .pBufferInfo = &descriptor_buffer_info,
                                               .pTexelBufferView = nullptr};
  vkUpdateDescriptorSets(vk_device_,
                         1,  // descriptorWriteCount
                         &write_descriptor_set,
                         0,         // descriptorCopyCount
                         nullptr);  // pDescriptorCopies

  VkPipelineLayout pipeline_layout;

  VkPipelineLayoutCreateInfo pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptor_set_layout,
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = nullptr};

  if ((result = vkCreatePipelineLayout(vk_device_, &pipeline_create_info, nullptr,
                                       &pipeline_layout)) != VK_SUCCESS) {
    PRINT_STDERR("vkCreatePipelineLayout failed: %d", result);
    return false;
  }

  VkPipeline compute_pipeline;

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
      .layout = pipeline_layout,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = 0};

  if ((result = vkCreateComputePipelines(vk_device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                         &compute_pipeline)) != VK_SUCCESS) {
    PRINT_STDERR("vkCreateComputePipelines failed: %d", result);
    return false;
  }

  if (hang_on_event_) {
    VkEvent event;
    VkEventCreateInfo event_info = {
        .sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO, .pNext = nullptr, .flags = 0};
    if ((result = vkCreateEvent(vk_device_, &event_info, nullptr, &event)) != VK_SUCCESS) {
      PRINT_STDERR("vkCreateEvent failed: %d", result);
      return false;
    }

    vkCmdWaitEvents(vk_command_buffer_, 1, &event, VK_PIPELINE_STAGE_HOST_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr, 0, nullptr);
  } else {
    vkCmdBindPipeline(vk_command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);

    vkCmdBindDescriptorSets(vk_command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
                            0,  // firstSet
                            1,  // descriptorSetCount,
                            &descriptor_set,
                            0,         // dynamicOffsetCount
                            nullptr);  // pDynamicOffsets

    vkCmdDispatch(vk_command_buffer_, 1, 1, 1);
  }

  if ((result = vkEndCommandBuffer(vk_command_buffer_)) != VK_SUCCESS) {
    PRINT_STDERR("vkEndCommandBuffer failed: %d", result);
    return false;
  }

  return true;
}

bool VkLoopTest::Exec(bool kill_driver) {
  VkResult result;
  result = vkQueueWaitIdle(vk_queue_);
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkQueueWaitIdle failed with result %d", result);
    return false;
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

  if ((result = vkQueueSubmit(vk_queue_, 1, &submit_info, VK_NULL_HANDLE)) != VK_SUCCESS) {
    PRINT_STDERR("vkQueueSubmit failed");
    return false;
  }

  if (kill_driver) {
    magma::TestDeviceBase test_device(vendor_id_);
    uint64_t is_supported = 0;
    magma_status_t status =
        magma_query2(test_device.device(), MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED, &is_supported);
    if (status != MAGMA_STATUS_OK || !is_supported) {
      printf("Test restart not supported: status %d is_supported %lu", status, is_supported);
      return true;
    }

    // TODO: Unbind and rebind driver once that supports forcibly tearing down client connections.
    EXPECT_EQ(ZX_OK, fuchsia_gpu_magma_DeviceTestRestart(test_device.channel()->get()));
  }

  for (int i = 0; i < 5; i++) {
    result = vkQueueWaitIdle(vk_queue_);
    if (result != VK_SUCCESS)
      break;
  }
  if (result != VK_ERROR_DEVICE_LOST) {
    PRINT_STDERR("Result was %d instead of VK_ERROR_DEVICE_LOST", result);
    return false;
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
