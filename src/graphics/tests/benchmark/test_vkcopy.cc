// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdlib.h>

#include <chrono>
#include <vector>

#include "src/graphics/tests/common/utils.h"
#include "src/graphics/tests/common/vulkan_context.h"

#include <vulkan/vulkan.hpp>

namespace {

constexpr size_t kNumBuffers = 2;
constexpr size_t kSrcBuffer = 0;
constexpr size_t kDstBuffer = 1;
constexpr uint8_t kSrcValue = 0xaa;

}  // namespace

class VkCopyTest {
 public:
  explicit VkCopyTest(uint32_t buffer_size) : buffer_size_(buffer_size) {}
  virtual ~VkCopyTest();

  bool Initialize();
  bool Exec();

 private:
  bool InitBuffers(uint32_t buffer_size);

  bool is_initialized_ = false;
  uint32_t buffer_size_;
  std::unique_ptr<VulkanContext> ctx_;

  struct Buffer {
    vk::BufferUsageFlagBits usage;
    vk::UniqueBuffer buffer;
    vk::UniqueDeviceMemory memory;
  };
  std::array<Buffer, kNumBuffers> buffers_;
  vk::UniqueCommandPool command_pool_;
  std::vector<vk::CommandBuffer> command_buffers_;
};

VkCopyTest::~VkCopyTest() {
  if (is_initialized_) {
    ctx_->device()->freeCommandBuffers(*command_pool_, command_buffers_);
  }
  is_initialized_ = false;
}

bool VkCopyTest::Initialize() {
  if (is_initialized_) {
    return false;
  }

  ctx_ = VulkanContext::Builder{}.set_validation_layers_enabled(false).Unique();

  if (!ctx_) {
    RTN_MSG(false, "Failed to initialize Vulkan.\n");
  }

  if (!InitBuffers(buffer_size_)) {
    RTN_MSG(false, "InitBuffers failed.\n");
  }

  is_initialized_ = true;

  return true;
}

bool VkCopyTest::InitBuffers(uint32_t buffer_size) {
  vk::Result rv;
  const auto &device = ctx_->device();

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
    RTN_MSG(false, "Can't find compatible mappable memory for image.\n");
  }

  buffers_[kSrcBuffer].usage = vk::BufferUsageFlagBits::eTransferSrc;
  buffers_[kDstBuffer].usage = vk::BufferUsageFlagBits::eTransferDst;
  for (auto &buffer : buffers_) {
    vk::BufferCreateInfo buffer_info;
    buffer_info.size = buffer_size;
    buffer_info.usage = buffer.usage;
    buffer_info.sharingMode = vk::SharingMode::eExclusive;

    auto rvt_buffer = device->createBufferUnique(buffer_info);
    if (vk::Result::eSuccess != rvt_buffer.result) {
      RTN_MSG(false, "VK Error: 0x%x - Create buffer.\n", rvt_buffer.result);
    }
    buffer.buffer = std::move(rvt_buffer.value);

    vk::MemoryAllocateInfo alloc_info;
    alloc_info.allocationSize = buffer_size;
    alloc_info.memoryTypeIndex = memory_type;

    auto rvt_memory = device->allocateMemoryUnique(alloc_info);
    if (vk::Result::eSuccess != rvt_memory.result) {
      RTN_MSG(false, "VK Error: 0x%x - Create buffer memory.\n", rvt_memory.result);
    }
    buffer.memory = std::move(rvt_memory.value);

    void *addr;
    rv = device->mapMemory(*(buffer.memory), 0 /* offset */, buffer_size, vk::MemoryMapFlags(),
                           &addr);
    if (vk::Result::eSuccess != rv) {
      RTN_MSG(false, "VK Error: 0x%x - Map buffer memory.\n", rv);
    }

    uint8_t index = (buffer.usage == vk::BufferUsageFlagBits::eTransferSrc) ? 0 : 1;
    memset(addr, static_cast<uint8_t>(kSrcValue + index), buffer_size);
    device->unmapMemory(*(buffer.memory));

    rv = device->bindBufferMemory(*(buffer.buffer), *(buffer.memory), 0 /* offset */);
    if (rv != vk::Result::eSuccess) {
      RTN_MSG(false, "VK Error: 0x%x - Bind buffer memory.\n", rv);
    }
  }

  vk::CommandPoolCreateInfo command_pool_info;
  command_pool_info.queueFamilyIndex = ctx_->queue_family_index();

  auto rvt_command_pool = device->createCommandPoolUnique(command_pool_info);
  if (vk::Result::eSuccess != rvt_command_pool.result) {
    RTN_MSG(false, "VK Error: 0x%x - Create command pool.\n", rvt_command_pool.result);
  }
  command_pool_ = std::move(rvt_command_pool.value);

  vk::CommandBufferAllocateInfo cmd_buff_alloc_info;
  cmd_buff_alloc_info.commandPool = *command_pool_;
  cmd_buff_alloc_info.level = vk::CommandBufferLevel::ePrimary;
  cmd_buff_alloc_info.commandBufferCount = 1;

  auto rvt_alloc_cmd_bufs = device->allocateCommandBuffers(cmd_buff_alloc_info);
  if (vk::Result::eSuccess != rvt_alloc_cmd_bufs.result) {
    RTN_MSG(false, "VK Error: 0x%x - Allocate command buffers.\n", rvt_alloc_cmd_bufs.result);
  }
  command_buffers_ = std::move(rvt_alloc_cmd_bufs.value);
  vk::CommandBuffer &command_buffer = command_buffers_.front();

  auto rv_begin = command_buffer.begin(vk::CommandBufferBeginInfo{});
  if (vk::Result::eSuccess != rv_begin) {
    RTN_MSG(false, "VK Error: 0x%x - Begin command buffer.\n", rv_begin);
  }

  vk::BufferCopy copy_region(0 /* srcOffset */, 0 /* dstOffset */, buffer_size);
  command_buffer.copyBuffer(*(buffers_[kSrcBuffer].buffer), *(buffers_[kDstBuffer].buffer),
                            1 /* regionCount */, &copy_region);

  auto rv_end = command_buffer.end();
  if (vk::Result::eSuccess != rv_end) {
    RTN_MSG(false, "VK Error: 0x%x - End command buffer.\n", rv_end);
  }

  return true;
}

bool VkCopyTest::Exec() {
  const auto &device = ctx_->device();

  // Submit command buffer and wait for it to complete.
  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = static_cast<uint32_t>(command_buffers_.size());
  submit_info.pCommandBuffers = command_buffers_.data();

  auto rv = ctx_->queue().submit(1 /* submitCt */, &submit_info, nullptr /* fence */);
  if (rv != vk::Result::eSuccess) {
    RTN_MSG(false, "VK Error: 0x%x - vk::Queue submit failed.\n", rv);
  }

  ctx_->queue().waitIdle();

  // Verify that we've copied from kSrcBuffer to kDstBuffer.
  void *dst_addr;
  auto rv_map = device->mapMemory(*(buffers_[kDstBuffer].memory), 0 /* offset */, buffer_size_,
                                  vk::MemoryMapFlags(), &dst_addr);
  if (vk::Result::eSuccess != rv_map) {
    RTN_MSG(false, "VK Error: 0x%x - Map buffer memory, value test.\n", rv_map);
  }
  if (*(static_cast<uint8_t *>(dst_addr)) != kSrcValue) {
    RTN_MSG(false, "Dst buffer contents don't match src buffer - copy failed.\n");
  }
  device->unmapMemory(*(buffers_[kDstBuffer].memory));

  return true;
}

int main() {
  constexpr uint32_t kBufferSize = 60 * 1024 * 1024;
  constexpr uint32_t kIterations = 1000;

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
