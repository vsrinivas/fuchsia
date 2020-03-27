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

const size_t kNumBuffers = 2;
const size_t kSrcBuffer = 0;
const size_t kDstBuffer = 1;

}  // namespace

class VkCopyTest {
 public:
  explicit VkCopyTest(uint32_t buffer_size) : buffer_size_(buffer_size) {}
  ~VkCopyTest();

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
  std::vector<VkCommandBuffer> command_buffers_;
};

VkCopyTest::~VkCopyTest() {
  if (is_initialized_) {
    vkFreeCommandBuffers(*ctx_->device(), *command_pool_, command_buffers_.size(),
                         command_buffers_.data());
  }
  is_initialized_ = false;
}

bool VkCopyTest::Initialize() {
  if (is_initialized_) {
    return false;
  }

  ctx_ = VulkanContext::Builder{}.Unique();

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

    auto rvt_buffer = ctx_->device()->createBufferUnique(buffer_info);
    if (vk::Result::eSuccess != rvt_buffer.result) {
      RTN_MSG(false, "VK Error: 0x%x - Create buffer.\n", rvt_buffer.result);
    }
    buffer.buffer = std::move(rvt_buffer.value);

    vk::MemoryAllocateInfo alloc_info;
    alloc_info.allocationSize = buffer_size;
    alloc_info.memoryTypeIndex = memory_type;

    auto rvt_memory = ctx_->device()->allocateMemoryUnique(alloc_info);
    if (vk::Result::eSuccess != rvt_memory.result) {
      RTN_MSG(false, "VK Error: 0x%x - Create buffer memory.\n", rvt_memory.result);
    }
    buffer.memory = std::move(rvt_memory.value);

    void *addr;
    rv = ctx_->device()->mapMemory(*(buffer.memory), 0 /* offset */, buffer_size,
                                   vk::MemoryMapFlags(), &addr);
    if (vk::Result::eSuccess != rv) {
      RTN_MSG(false, "VK Error: 0x%x - Map buffer memory.\n", rv);
    }

    uint8_t index = (buffer.usage == vk::BufferUsageFlagBits::eTransferSrc) ? 0 : 1;
    memset(addr, static_cast<uint8_t>(index), buffer_size);
    ctx_->device()->unmapMemory(*(buffer.memory));

    rv = ctx_->device()->bindBufferMemory(*(buffer.buffer), *(buffer.memory), 0 /* offset */);
    if (rv != vk::Result::eSuccess) {
      RTN_MSG(false, "VK Error: 0x%x - Bind buffer memory.\n", rv);
    }
  }

  vk::CommandPoolCreateInfo command_pool_info;
  command_pool_info.queueFamilyIndex = ctx_->queue_family_index();

  auto rvt_command_pool = ctx_->device()->createCommandPoolUnique(command_pool_info);
  if (vk::Result::eSuccess != rvt_command_pool.result) {
    RTN_MSG(false, "VK Error: 0x%x - Create command pool.\n", rvt_command_pool.result);
  }
  command_pool_ = std::move(rvt_command_pool.value);

  VkCommandBufferAllocateInfo command_buffer_allocate_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = *command_pool_,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  command_buffers_.resize(command_buffer_allocate_info.commandBufferCount);
  auto result = vkAllocateCommandBuffers(*ctx_->device(), &command_buffer_allocate_info,
                                         command_buffers_.data());
  if (VK_SUCCESS != result) {
    RTN_MSG(false, "VK Error: 0x%x - Allocate command buffers.\n", result);
  }
  VkCommandBuffer &command_buffer = command_buffers_.front();

  VkCommandBufferBeginInfo command_buffer_begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = 0,
      .pInheritanceInfo = nullptr,  // ignored for primary buffers
  };
  result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
  if (VK_SUCCESS != result) {
    RTN_MSG(false, "VK Error: 0x%x - Begin command buffer.\n", result);
  }

  VkBufferCopy copy_region = {0 /* srcOffset */, 0 /* dstOffset */, buffer_size};
  vkCmdCopyBuffer(command_buffer, *(buffers_[kSrcBuffer].buffer), *(buffers_[kDstBuffer].buffer),
                  1 /* regionCount */, &copy_region);

  result = vkEndCommandBuffer(command_buffer);
  if (VK_SUCCESS != result) {
    RTN_MSG(false, "VK Error: 0x%x - End command buffer.\n", result);
  }

  return true;
}

bool VkCopyTest::Exec() {
  VkCommandBuffer &command_buffer = command_buffers_.front();
  VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = nullptr,
      0 /* waitSemaphoreCount */,
      nullptr /* pWaitSemaphores */,
      nullptr /* pWaitDstStageMask */,
      static_cast<uint32_t>(command_buffers_.size()),
      command_buffers_.data(),
      0 /* signalSemaphoreCount */,
      nullptr /* pSignalSemaphores */
  };
  vk::SubmitInfo submit_info_hpp(submit_info);
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;

  auto rv = ctx_->queue().submit(1 /* submitCt */, &submit_info_hpp, nullptr /* fence */);
  if (rv != vk::Result::eSuccess) {
    RTN_MSG(false, "VK Error: 0x%x - vk::Queue submit failed.\n", rv);
  }

  ctx_->queue().waitIdle();

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
