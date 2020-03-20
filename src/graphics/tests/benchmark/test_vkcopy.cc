// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <vector>

#include <vulkan/vulkan.hpp>

#define RTN_MSG(err, ...)                          \
  {                                                \
    fprintf(stderr, "%s:%d ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__);                  \
    fflush(stderr);                                \
    return err;                                    \
  }

class VkCopyTest {
 public:
  explicit VkCopyTest(uint32_t buffer_size) : buffer_size_(buffer_size) {}

  bool Initialize();
  bool Exec();

 private:
  bool InitVulkan();
  bool InitBuffers(uint32_t buffer_size);

  bool is_initialized_ = false;
  uint32_t buffer_size_;
  vk::UniqueInstance instance_;
  vk::PhysicalDevice physical_device_;
  vk::UniqueDevice device_;
  vk::Queue queue_;

  struct Buffer {
    vk::BufferUsageFlagBits usage;
    vk::UniqueBuffer buffer;
    vk::UniqueDeviceMemory memory;
  };
  std::array<Buffer, 2> buffers_;
  vk::UniqueCommandPool command_pool_;
  std::vector<vk::UniqueCommandBuffer> command_buffers_;
};

bool VkCopyTest::Initialize() {
  if (is_initialized_) {
    return false;
  }

  if (!InitVulkan()) {
    RTN_MSG(false, "Failed to initialize Vulkan.\n");
  }

  if (!InitBuffers(buffer_size_)) {
    RTN_MSG(false, "InitBuffers failed.\n");
  }

  is_initialized_ = true;

  return true;
}

bool VkCopyTest::InitVulkan() {
  // All vulkan hpp create functions will follow the pattern below.
  // An 'auto' declaration of a result value type specific to the call. e.g. below:
  //   ResultValueType<UniqueHandle<class Instance, class DispatchLoaderStatic>>::type
  // is the type of rvt_instance.
  vk::InstanceCreateInfo instance_info;
  auto rvt_instance = vk::createInstanceUnique(instance_info);
  if (vk::Result::eSuccess != rvt_instance.result) {
    RTN_MSG(false, "VK Error: 0x%x - Create instance.\n", rvt_instance.result);
  }
  instance_ = std::move(rvt_instance.value);

  auto [rv, physical_devices] = instance_->enumeratePhysicalDevices();
  if (vk::Result::eSuccess != rv || physical_devices.empty()) {
    RTN_MSG(false, "VK Error: 0x%x - No physical device found.\n", rv);
  }
  physical_device_ = physical_devices[0];

  const auto queue_families = physical_device_.getQueueFamilyProperties();
  size_t queue_family_index = queue_families.size();
  for (size_t i = 0; i < queue_families.size(); ++i) {
    if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
      queue_family_index = i;
      break;
    }
  }
  if (queue_family_index == queue_families.size()) {
    RTN_MSG(false, "Couldn't find an appropriate queue.\n");
  }

  float queue_priority = 0.0f;
  vk::DeviceQueueCreateInfo queue_info;
  queue_info.queueCount = 1;
  queue_info.queueFamilyIndex = queue_family_index;
  queue_info.pQueuePriorities = &queue_priority;

  vk::DeviceCreateInfo device_info;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;

  auto rvt_device = physical_device_.createDeviceUnique(device_info);
  if (vk::Result::eSuccess != rvt_device.result) {
    RTN_MSG(false, "VK Error: 0x%x - Create device.\n", rvt_device.result);
  }
  device_ = std::move(rvt_device.value);
  queue_ = device_->getQueue(queue_family_index, 0);

  return true;
}

bool VkCopyTest::InitBuffers(uint32_t buffer_size) {
  vk::Result rv;

  vk::PhysicalDeviceMemoryProperties memory_props;
  physical_device_.getMemoryProperties(&memory_props);
  uint32_t memory_type = 0;
  for (; memory_type < memory_props.memoryTypeCount; memory_type++) {
    if (memory_props.memoryTypes[memory_type].propertyFlags &
        vk::MemoryPropertyFlagBits::eHostVisible) {
      break;
    }
  }
  if (memory_type >= memory_props.memoryTypeCount) {
    RTN_MSG(false, "Can't find compatible mappable memory for image.\n");
  }

  buffers_[0].usage = vk::BufferUsageFlagBits::eTransferSrc;
  buffers_[1].usage = vk::BufferUsageFlagBits::eTransferDst;
  for (auto &buffer : buffers_) {
    vk::BufferCreateInfo buffer_info;
    buffer_info.size = buffer_size;
    buffer_info.usage = buffer.usage;
    buffer_info.sharingMode = vk::SharingMode::eExclusive;

    auto rvt_buffer = device_->createBufferUnique(buffer_info);
    if (vk::Result::eSuccess != rvt_buffer.result) {
      RTN_MSG(false, "VK Error: 0x%x - Create buffer.\n", rvt_buffer.result);
    }
    buffer.buffer = std::move(rvt_buffer.value);

    vk::MemoryAllocateInfo alloc_info;
    alloc_info.allocationSize = buffer_size;
    alloc_info.memoryTypeIndex = memory_type;

    auto rvt_memory = device_->allocateMemoryUnique(alloc_info);
    if (vk::Result::eSuccess != rvt_memory.result) {
      RTN_MSG(false, "VK Error: 0x%x - Create buffer memory.\n", rvt_memory.result);
    }
    buffer.memory = std::move(rvt_memory.value);

    void *addr;
    rv = device_->mapMemory(*(buffer.memory), 0 /* offset */, buffer_size, vk::MemoryMapFlags(),
                            &addr);
    if (vk::Result::eSuccess != rv) {
      RTN_MSG(false, "VK Error: 0x%x - Map buffer memory.\n", rv);
    }

    uint8_t index = (buffer.usage == vk::BufferUsageFlagBits::eTransferSrc) ? 0 : 1;
    memset(addr, static_cast<uint8_t>(index), buffer_size);
    device_->unmapMemory(*(buffer.memory));

    rv = device_->bindBufferMemory(*(buffer.buffer), *(buffer.memory), 0 /* offset */);
    if (rv != vk::Result::eSuccess) {
      RTN_MSG(false, "VK Error: 0x%x - Bind buffer memory.\n", rv);
    }
  }

  vk::CommandPoolCreateInfo command_pool_info;
  command_pool_info.queueFamilyIndex = 0;

  auto rvt_command_pool = device_->createCommandPoolUnique(command_pool_info);
  if (vk::Result::eSuccess != rvt_command_pool.result) {
    RTN_MSG(false, "VK Error: 0x%x - Create command pool.\n", rvt_command_pool.result);
  }
  command_pool_ = std::move(rvt_command_pool.value);

  vk::CommandBufferAllocateInfo command_buffer_allocate_info;
  command_buffer_allocate_info.commandPool = *command_pool_;
  command_buffer_allocate_info.level = vk::CommandBufferLevel::ePrimary;
  command_buffer_allocate_info.commandBufferCount = 1;
  auto rvt_command_buffers = device_->allocateCommandBuffersUnique(command_buffer_allocate_info);
  if (vk::Result::eSuccess != rvt_command_buffers.result) {
    RTN_MSG(false, "VK Error: 0x%x - Allocate command buffers.\n", rvt_command_buffers.result);
  }
  command_buffers_ = std::move(rvt_command_buffers.value);
  vk::UniqueCommandBuffer &command_buffer = command_buffers_[0];

  rv = command_buffer->begin(vk::CommandBufferBeginInfo{});
  if (vk::Result::eSuccess != rv) {
    RTN_MSG(false, "VK Error: 0x%x - Begin command buffer.\n", rv);
  }

  vk::BufferCopy copy_region(0 /* srcOffset */, 0 /* dstOffset */, buffer_size);
  command_buffer->copyBuffer(*buffers_[0].buffer, *buffers_[1].buffer, 1 /* region_count */,
                             &copy_region);

  rv = command_buffer->end();
  if (rv != vk::Result::eSuccess) {
    RTN_MSG(false, "VK Error: 0x%x - End command buffer.\n", rv);
  }

  return true;
}

bool VkCopyTest::Exec() {
  auto &command_buffer = command_buffers_[0];
  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &(command_buffer.get());

  auto rv = queue_.submit(1, &submit_info, nullptr /* fence */);
  if (rv != vk::Result::eSuccess) {
    RTN_MSG(false, "VK Error: 0x%x - vk::Queue submit failed.\n", rv);
  }

  queue_.waitIdle();

  return true;
}

int main() {
  const uint32_t kBufferSize = 60 * 1024 * 1024;
  const uint32_t kIterations = 1000;

  VkCopyTest app(kBufferSize);

  if (!app.Initialize()) {
    RTN_MSG(-1, "Could not initialize app.\n");
  }

  printf("Copying buffer size: %u  Iterations: %u...\n", kBufferSize, kIterations);
  fflush(stdout);

  auto start = std::chrono::high_resolution_clock::now();

  for (uint32_t iter = 0; iter < kIterations; iter++) {
    if (!app.Exec()) {
      RTN_MSG(-1, "Exec failed.\n");
    }
  }

  std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - start;

  const uint32_t kMB = 1024 * 1024;
  printf("Copy rate %g MB/s\n",
         static_cast<double>(kBufferSize) * kIterations / kMB / elapsed.count());
  fflush(stdout);

  return 0;
}
