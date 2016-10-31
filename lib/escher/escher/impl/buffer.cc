// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/buffer.h"

#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

namespace {

inline vk::Buffer CreateBufferHelper(vk::Device device,
                                     vk::DeviceSize size,
                                     vk::BufferUsageFlags usage_flags) {
  vk::BufferCreateInfo info;
  info.size = size;
  info.usage = usage_flags;
  info.sharingMode = vk::SharingMode::eExclusive;
  return ESCHER_CHECKED_VK_RESULT(device.createBuffer(info));
}

inline GpuMemPtr AllocateMemoryHelper(vk::Device device,
                                      GpuAllocator* allocator,
                                      vk::Buffer buffer,
                                      vk::MemoryPropertyFlags flags) {
  vk::MemoryRequirements reqs = device.getBufferMemoryRequirements(buffer);
  return allocator->Allocate(reqs, flags);
}

}  // namespace

Buffer::Buffer(vk::Device device,
               GpuAllocator* allocator,
               vk::DeviceSize size,
               vk::BufferUsageFlags usage_flags,
               vk::MemoryPropertyFlags memory_property_flags)
    : device_(device),
      allocator_(allocator),
      usage_flags_(usage_flags),
      memory_property_flags_(memory_property_flags),
      buffer_(CreateBufferHelper(device, size, usage_flags)),
      mem_(AllocateMemoryHelper(device,
                                allocator,
                                buffer_,
                                memory_property_flags)) {
  device.bindBufferMemory(buffer_, mem_->base(), mem_->offset());
}

Buffer::Buffer(Buffer&& other)
    : device_(std::move(other.device_)),
      allocator_(other.allocator_),
      usage_flags_(other.usage_flags_),
      memory_property_flags_(other.memory_property_flags_),
      buffer_(std::move(other.buffer_)),
      mem_(std::move(other.mem_)),
      mapped_(other.mapped_) {
  other.device_ = nullptr;
  other.allocator_ = nullptr;
  other.mapped_ = nullptr;
}

Buffer::~Buffer() {
  // TODO: register vk::Buffer for destruction somewhere.
  if (device_) {
    device_.destroyBuffer(buffer_);
  }
}

uint8_t* Buffer::Map() {
  if (!mapped_) {
    mapped_ = ESCHER_CHECKED_VK_RESULT(
        device_.mapMemory(mem_->base(), mem_->offset(), mem_->size()));
  }
  return reinterpret_cast<uint8_t*>(mapped_);
}

void Buffer::Unmap() {
  if (mapped_) {
    // TODO: only flush if the coherent bit isn't set.
    vk::MappedMemoryRange range;
    range.memory = mem_->base();
    range.offset = mem_->offset();
    range.size = mem_->size();
    device_.flushMappedMemoryRanges(1, &range);

    device_.unmapMemory(mem_->base());
    mapped_ = nullptr;
  }
}

}  // namespace impl
}  // namespace escher
