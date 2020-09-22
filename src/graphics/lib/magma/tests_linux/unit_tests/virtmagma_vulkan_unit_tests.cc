// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

// TODO(fxbug.dev/27262): support shaders as a first-class target type
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnewline-eof"
#include "src/graphics/lib/magma/tests_linux/unit_tests/basic_compute.h"
#pragma clang diagnostic pop

#include <sstream>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace {

// Allows googletest to print VkResult values.
class VkResultPrintable {
 public:
  VkResultPrintable(VkResult result) : result_(result) {}
  VkResultPrintable& operator=(VkResult result) {
    result_ = result;
    return *this;
  }
  VkResultPrintable& operator=(const VkResultPrintable& other) {
    result_ = other.result_;
    return *this;
  }
  bool operator==(VkResult result) const { return result == result_; }
  bool operator==(VkResultPrintable& other) const { return other.result_ == result_; }
  friend std::ostream& operator<<(std::ostream& os, const VkResultPrintable& result) {
    switch (result.result_) {
      case VK_SUCCESS:
        return os << "VK_SUCCESS";
      case VK_NOT_READY:
        return os << "VK_NOT_READY";
      case VK_TIMEOUT:
        return os << "VK_TIMEOUT";
      case VK_EVENT_SET:
        return os << "VK_EVENT_SET";
      case VK_EVENT_RESET:
        return os << "VK_EVENT_RESET";
      case VK_INCOMPLETE:
        return os << "VK_INCOMPLETE";
      case VK_ERROR_OUT_OF_HOST_MEMORY:
        return os << "VK_ERROR_OUT_OF_HOST_MEMORY";
      case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return os << "VK_ERROR_OUT_OF_DEVICE_MEMORY";
      case VK_ERROR_INITIALIZATION_FAILED:
        return os << "VK_ERROR_INITIALIZATION_FAILED";
      case VK_ERROR_DEVICE_LOST:
        return os << "VK_ERROR_DEVICE_LOST";
      case VK_ERROR_MEMORY_MAP_FAILED:
        return os << "VK_ERROR_MEMORY_MAP_FAILED";
      case VK_ERROR_LAYER_NOT_PRESENT:
        return os << "VK_ERROR_LAYER_NOT_PRESENT";
      case VK_ERROR_EXTENSION_NOT_PRESENT:
        return os << "VK_ERROR_EXTENSION_NOT_PRESENT";
      case VK_ERROR_FEATURE_NOT_PRESENT:
        return os << "VK_ERROR_FEATURE_NOT_PRESENT";
      case VK_ERROR_INCOMPATIBLE_DRIVER:
        return os << "VK_ERROR_INCOMPATIBLE_DRIVER";
      case VK_ERROR_TOO_MANY_OBJECTS:
        return os << "VK_ERROR_TOO_MANY_OBJECTS";
      case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return os << "VK_ERROR_FORMAT_NOT_SUPPORTED";
      case VK_ERROR_FRAGMENTED_POOL:
        return os << "VK_ERROR_FRAGMENTED_POOL";
      case VK_ERROR_OUT_OF_POOL_MEMORY:
        return os << "VK_ERROR_OUT_OF_POOL_MEMORY";
      case VK_ERROR_INVALID_EXTERNAL_HANDLE:
        return os << "VK_ERROR_INVALID_EXTERNAL_HANDLE";
      case VK_ERROR_SURFACE_LOST_KHR:
        return os << "VK_ERROR_SURFACE_LOST_KHR";
      case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return os << "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
      case VK_SUBOPTIMAL_KHR:
        return os << "VK_SUBOPTIMAL_KHR";
      case VK_ERROR_OUT_OF_DATE_KHR:
        return os << "VK_ERROR_OUT_OF_DATE_KHR";
      case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return os << "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
      case VK_ERROR_VALIDATION_FAILED_EXT:
        return os << "VK_ERROR_VALIDATION_FAILED_EXT";
      case VK_ERROR_INVALID_SHADER_NV:
        return os << "VK_ERROR_INVALID_SHADER_NV";
      case VK_ERROR_FRAGMENTATION_EXT:
        return os << "VK_ERROR_FRAGMENTATION_EXT";
      case VK_ERROR_NOT_PERMITTED_EXT:
        return os << "VK_ERROR_NOT_PERMITTED_EXT";
      default:
        return os << "UNKNOWN (" << static_cast<int32_t>(result.result_) << ")";
    }
  }

 private:
  VkResult result_;
};

struct VulkanPhysicalDevice {
  VkPhysicalDevice device;
  VkPhysicalDeviceProperties properties;
  std::vector<VkQueueFamilyProperties> queues;
};

class VirtMagmaTest : public ::testing::Test {
 protected:
  VirtMagmaTest() {}

  ~VirtMagmaTest() override {}

  void SetUp() override {
    CreateInstance();
    EnumeratePhysicalDevices();
    GetQueues();
  }

  void TearDown() override {
    physical_devices_.clear();  // Implicitly destroyed with instance.
    vkDestroyInstance(instance_, nullptr);
  }

  void CreateInstance() {
    VkApplicationInfo application_info{};
    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pApplicationName = "fuchsia-test";
    application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application_info.pEngineName = "no-engine";
    application_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application_info.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instance_create_info{};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pApplicationInfo = &application_info;
    VkResultPrintable result = vkCreateInstance(&instance_create_info, nullptr, &instance_);
    ASSERT_EQ(result, VK_SUCCESS) << "vkCreateInstance failed";
  }

  void EnumeratePhysicalDevices() {
    uint32_t physical_device_count = 0;
    std::vector<VkPhysicalDevice> physical_devices;
    VkResultPrintable result =
        vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr);
    ASSERT_EQ(result, VK_SUCCESS) << "vkEnumeratePhysicalDevices failed";
    physical_devices.resize(physical_device_count);
    while ((result = vkEnumeratePhysicalDevices(instance_, &physical_device_count,
                                                physical_devices.data())) == VK_INCOMPLETE) {
      physical_devices.resize(++physical_device_count);
    }
    ASSERT_EQ(result, VK_SUCCESS) << "vkEnumeratePhysicalDevices failed";
    ASSERT_GT(physical_devices.size(), 0u) << "No physical devices found";
    physical_devices_.resize(physical_device_count);
    for (uint32_t i = 0; i < physical_device_count; ++i) {
      VkPhysicalDeviceProperties properties{};
      vkGetPhysicalDeviceProperties(physical_devices[i], &properties);
      EXPECT_NE(properties.vendorID, 0u) << "Missing vendor ID";
      EXPECT_NE(properties.deviceID, 0u) << "Missing device ID";
      EXPECT_LE(properties.vendorID, 0xFFFFu) << "Invalid vendor ID";
      EXPECT_LE(properties.deviceID, 0xFFFFu) << "Invalid device ID";
      physical_devices_[i].device = physical_devices[i];
      physical_devices_[i].properties = properties;
    }
  }

  void GetQueues() {
    for (auto& device : physical_devices_) {
      uint32_t queue_count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(device.device, &queue_count, nullptr);
      ASSERT_GT(queue_count, 0u) << "No queue families found";
      device.queues.resize(queue_count);
      vkGetPhysicalDeviceQueueFamilyProperties(device.device, &queue_count, device.queues.data());
      uint32_t queue_flags_union = 0;
      for (auto& queue : device.queues) {
        ASSERT_GT(queue.queueCount, 0u) << "Empty queue family";
        queue_flags_union |= queue.queueFlags;
      }
      ASSERT_TRUE(queue_flags_union & VK_QUEUE_GRAPHICS_BIT)
          << "Device missing graphics capability";
      ASSERT_TRUE(queue_flags_union & VK_QUEUE_COMPUTE_BIT) << "Device missing compute capability";
    }
  }

  VkInstance instance_;
  std::vector<VulkanPhysicalDevice> physical_devices_;
};

// Tests that a device can be created on the first reported graphics queue.
TEST_F(VirtMagmaTest, CreateGraphicsDevice) {
  // TODO(fxbug.dev/13224): support per-device gtests
  for (auto& physical_device : physical_devices_) {
    VkDeviceQueueCreateInfo device_queue_create_info{};
    device_queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    for (uint32_t i = 0; i < physical_device.queues.size(); ++i) {
      if (physical_device.queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        device_queue_create_info.queueFamilyIndex = i;
        break;
      }
    }
    device_queue_create_info.queueCount = 1;
    float priority = 1.0f;
    device_queue_create_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &device_queue_create_info;
    VkDevice device{};
    VkResultPrintable result =
        vkCreateDevice(physical_device.device, &device_create_info, nullptr, &device);
    ASSERT_EQ(result, VK_SUCCESS) << "vkCreateDevice failed";
    vkDestroyDevice(device, nullptr);
  }
}

// Tests that the device can run a basic compute shader.
TEST_F(VirtMagmaTest, BasicCompute) {
  static const size_t kBufferSize = 65536;
  static const uint32_t kGroupSize = 32;  // This must match basic_compute.glsl

  const uint32_t num_elements = kBufferSize / sizeof(uint32_t);
  const uint32_t num_groups = num_elements / kGroupSize;

  // TODO(fxbug.dev/13224): support per-device gtests
  for (auto& physical_device : physical_devices_) {
    VkDeviceQueueCreateInfo device_queue_create_info{};
    device_queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    for (uint32_t i = 0; i < physical_device.queues.size(); ++i) {
      if (physical_device.queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
        device_queue_create_info.queueFamilyIndex = i;
        break;
      }
    }
    device_queue_create_info.queueCount = 1;
    float priority = 1.0f;
    device_queue_create_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &device_queue_create_info;
    VkDevice device{};
    VkResultPrintable result =
        vkCreateDevice(physical_device.device, &device_create_info, nullptr, &device);
    ASSERT_EQ(result, VK_SUCCESS) << "vkCreateDevice failed";
    VkQueue queue{};
    vkGetDeviceQueue(device, device_queue_create_info.queueFamilyIndex, 0, &queue);

    VkPhysicalDeviceMemoryProperties physical_device_memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device.device, &physical_device_memory_properties);
    int32_t memory_type_index = -1;
    for (uint32_t i = 0; i < physical_device_memory_properties.memoryTypeCount; ++i) {
      const auto& memory_type = physical_device_memory_properties.memoryTypes[i];
      if ((memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
          (memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
          (physical_device_memory_properties.memoryHeaps[memory_type.heapIndex].size >=
           kBufferSize)) {
        memory_type_index = i;
        break;
      }
    }
    ASSERT_NE(memory_type_index, -1) << "Suitable memory heap not found";

    VkMemoryAllocateInfo memory_allocate_info{};
    memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_allocate_info.allocationSize = kBufferSize;
    memory_allocate_info.memoryTypeIndex = memory_type_index;
    VkDeviceMemory device_memory{};
    result = vkAllocateMemory(device, &memory_allocate_info, nullptr, &device_memory);
    ASSERT_EQ(result, VK_SUCCESS) << "vkAllocateMemory failed";

    VkBufferCreateInfo buffer_create_info{};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = kBufferSize;
    buffer_create_info.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer buffer{};
    result = vkCreateBuffer(device, &buffer_create_info, nullptr, &buffer);
    ASSERT_EQ(result, VK_SUCCESS) << "vkCreateBuffer failed";

    result = vkBindBufferMemory(device, buffer, device_memory, 0);
    ASSERT_EQ(result, VK_SUCCESS) << "vkBindBufferMemory failed";

    VkShaderModuleCreateInfo shader_module_create_info{};
    shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_create_info.codeSize = sizeof(basic_compute_spirv);
    shader_module_create_info.pCode = basic_compute_spirv;
    VkShaderModule shader_module{};
    result = vkCreateShaderModule(device, &shader_module_create_info, nullptr, &shader_module);
    ASSERT_EQ(result, VK_SUCCESS) << "vkCreateShaderModule failed";

    VkDescriptorSetLayoutBinding descriptor_set_layout_binding{};
    descriptor_set_layout_binding.binding = 0;
    descriptor_set_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptor_set_layout_binding.descriptorCount = 1;
    descriptor_set_layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{};
    descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_set_layout_create_info.bindingCount = 1;
    descriptor_set_layout_create_info.pBindings = &descriptor_set_layout_binding;
    VkDescriptorSetLayout descriptor_set_layout{};
    result = vkCreateDescriptorSetLayout(device, &descriptor_set_layout_create_info, nullptr,
                                         &descriptor_set_layout);
    ASSERT_EQ(result, VK_SUCCESS) << "vkCreateDescriptorSetLayout failed";

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo descriptor_pool_create_info{};
    descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_create_info.maxSets = 1;
    descriptor_pool_create_info.poolSizeCount = 1;
    descriptor_pool_create_info.pPoolSizes = &pool_size;
    VkDescriptorPool descriptor_pool{};
    result = vkCreateDescriptorPool(device, &descriptor_pool_create_info, 0, &descriptor_pool);
    ASSERT_EQ(result, VK_SUCCESS) << "vkCreateDescriptorPool failed";

    VkDescriptorSetAllocateInfo descriptor_set_allocate_info{};
    descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_set_allocate_info.descriptorPool = descriptor_pool;
    descriptor_set_allocate_info.descriptorSetCount = 1;
    descriptor_set_allocate_info.pSetLayouts = &descriptor_set_layout;
    VkDescriptorSet descriptor_set{};
    result = vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, &descriptor_set);
    ASSERT_EQ(result, VK_SUCCESS) << "vkAllocateDescriptorSets failed";

    VkDescriptorBufferInfo descriptor_buffer_info{};
    descriptor_buffer_info.buffer = buffer;
    descriptor_buffer_info.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet write_descriptor_set{};
    write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set.dstSet = descriptor_set;
    write_descriptor_set.descriptorCount = 1;
    write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_descriptor_set.pBufferInfo = &descriptor_buffer_info;
    vkUpdateDescriptorSets(device, 1, &write_descriptor_set, 0, nullptr);

    VkPipelineLayoutCreateInfo pipeline_layout_create_info{};
    pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = 1;
    pipeline_layout_create_info.pSetLayouts = &descriptor_set_layout;
    VkPipelineLayout pipeline_layout{};
    result =
        vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipeline_layout);
    ASSERT_EQ(result, VK_SUCCESS) << "vkCreatePipelineLayout failed";

    VkComputePipelineCreateInfo compute_pipeline_create_info{};
    compute_pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute_pipeline_create_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute_pipeline_create_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute_pipeline_create_info.stage.module = shader_module;
    compute_pipeline_create_info.stage.pName = "main";
    compute_pipeline_create_info.layout = pipeline_layout;
    VkPipeline pipeline{};
    result = vkCreateComputePipelines(device, nullptr, 1, &compute_pipeline_create_info, nullptr,
                                      &pipeline);
    ASSERT_EQ(result, VK_SUCCESS) << "vkCreateComputePipelines failed";

    VkCommandPoolCreateInfo command_pool_create_info{};
    command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_create_info.queueFamilyIndex = device_queue_create_info.queueFamilyIndex;
    VkCommandPool command_pool{};
    result = vkCreateCommandPool(device, &command_pool_create_info, nullptr, &command_pool);
    ASSERT_EQ(result, VK_SUCCESS) << "vkCreateCommandPool failed";

    VkCommandBufferAllocateInfo command_buffer_allocate_info{};
    command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocate_info.commandPool = command_pool;
    command_buffer_allocate_info.commandBufferCount = 1;
    VkCommandBuffer command_buffer{};
    result = vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &command_buffer);
    ASSERT_EQ(result, VK_SUCCESS) << "vkAllocateCommandBuffers failed";

    VkCommandBufferBeginInfo command_buffer_begin_info{};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
    ASSERT_EQ(result, VK_SUCCESS) << "vkBeginCommandBuffer failed";

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
                            &descriptor_set, 0, nullptr);
    vkCmdDispatch(command_buffer, num_groups, 1, 1);
    result = vkEndCommandBuffer(command_buffer);
    ASSERT_EQ(result, VK_SUCCESS) << "vkEndCommandBuffer failed";

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    result = vkQueueSubmit(queue, 1, &submit_info, nullptr);
    ASSERT_EQ(result, VK_SUCCESS) << "vkQueueSubmit failed";

    result = vkQueueWaitIdle(queue);
    ASSERT_EQ(result, VK_SUCCESS) << "vkQueueWaitIdle failed";

    void* mapped_data = nullptr;
    result = vkMapMemory(device, device_memory, 0, kBufferSize, 0, &mapped_data);
    ASSERT_EQ(result, VK_SUCCESS) << "vkMapMemory failed";

    auto buffer_data = reinterpret_cast<uint32_t*>(mapped_data);
    uint32_t correct_data_count = 0;
    for (uint32_t i = 0; i < num_elements; ++i) {
      uint32_t expected_value = i;
      if (buffer_data[i] == expected_value) {
        ++correct_data_count;
      }
    }
    EXPECT_EQ(correct_data_count, num_elements) << "Buffer does not contain the correct data";
  }
}

}  // namespace
