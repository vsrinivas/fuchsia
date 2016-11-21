// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/uniform_buffer_pool.h"

#include "escher/impl/gpu_allocator.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

// TODO: obtain max uniform-buffer size from Vulkan.  64kB is typical.
constexpr vk::DeviceSize kBufferSize = 65536;

UniformBufferPool::UniformBufferInfo::UniformBufferInfo(vk::Buffer b,
                                                        uint8_t* p)
    : buffer(b), ptr(p) {
  FTL_DCHECK(buffer);
}

UniformBufferPool::UniformBufferInfo::~UniformBufferInfo() {
  // Pool is responsible for destroying the vk::Buffer.
  FTL_DCHECK(!buffer);
}

vk::Buffer UniformBufferPool::UniformBufferInfo::GetBuffer() {
  return buffer;
}

vk::DeviceSize UniformBufferPool::UniformBufferInfo::GetSize() {
  return kBufferSize;
}

uint8_t* UniformBufferPool::UniformBufferInfo::GetMappedPointer() {
  return ptr;
}

UniformBufferPool::UniformBufferPool(vk::Device device, GpuAllocator* allocator)
    : device_(device),
      allocator_(allocator),
      flags_(vk::MemoryPropertyFlagBits::eHostVisible |
             vk::MemoryPropertyFlagBits::eHostCoherent),
      buffer_size_(kBufferSize) {}

UniformBufferPool::~UniformBufferPool() {
  FTL_CHECK(allocation_count_ == 0);
  for (auto& info : free_buffers_) {
    auto uniform_buffer_info = static_cast<UniformBufferInfo*>(info.get());
    device_.destroyBuffer(uniform_buffer_info->buffer);
    uniform_buffer_info->buffer = nullptr;
  }
}

BufferPtr UniformBufferPool::Allocate() {
  if (free_buffers_.empty()) {
    InternalAllocate();
  }
  auto buf = NewBuffer(std::move(free_buffers_.back()));
  free_buffers_.pop_back();
  ++allocation_count_;
  return buf;
}

void UniformBufferPool::RecycleBuffer(std::unique_ptr<BufferInfo> info) {
  --allocation_count_;
  free_buffers_.push_back(std::move(info));
}

void UniformBufferPool::InternalAllocate() {
  // Create a batch of buffers.
  constexpr uint32_t kBufferBatchSize = 10;
  vk::Buffer new_buffers[kBufferBatchSize];
  vk::BufferCreateInfo info;
  info.size = buffer_size_;
  info.usage = vk::BufferUsageFlagBits::eUniformBuffer;
  info.sharingMode = vk::SharingMode::eExclusive;
  for (uint32_t i = 0; i < kBufferBatchSize; ++i) {
    new_buffers[i] = ESCHER_CHECKED_VK_RESULT(device_.createBuffer(info));
  }

  // Determine the memory requirements for a single buffer.
  vk::MemoryRequirements reqs =
      device_.getBufferMemoryRequirements(new_buffers[0]);
  // If necessary, we can write the logic to deal with the conditions below.
  FTL_CHECK(buffer_size_ == reqs.size);
  FTL_CHECK(buffer_size_ % reqs.alignment == 0);

  // Allocate enough memory for all of the buffers.
  reqs.size *= kBufferBatchSize;
  auto mem = allocator_->Allocate(reqs, flags_);
  backing_memory_.push_back(mem);

  // Map the memory; we will associate a mapped pointer with each buffer.
  void* void_ptr = ESCHER_CHECKED_VK_RESULT(
      device_.mapMemory(mem->base(), mem->offset(), mem->size()));
  uint8_t* ptr = reinterpret_cast<uint8_t*>(void_ptr);

  // Finish up: bind each buffer to memory.
  vk::DeviceSize offset = mem->offset();
  for (uint32_t i = 0; i < kBufferBatchSize; ++i) {
    device_.bindBufferMemory(new_buffers[i], mem->base(), offset);
    free_buffers_.push_back(
        std::make_unique<UniformBufferInfo>(new_buffers[i], ptr));
    offset += buffer_size_;
    ptr += buffer_size_;
  }
}

}  // namespace impl
}  // namespace escher
