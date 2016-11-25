// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/vk/buffer.h"

#include "escher/impl/gpu_allocator.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {

namespace {

// Hidden concrete implementation of BufferInfo that is used when a Buffer
// has no owner.
class UnownedBufferInfo : public BufferInfo {
 public:
  UnownedBufferInfo(vk::Device device,
                    impl::GpuAllocator* allocator,
                    vk::DeviceSize size,
                    vk::BufferUsageFlags usage_flags,
                    vk::MemoryPropertyFlags memory_property_flags)
      : device_(device), size_(size), mapped_ptr_(nullptr) {
    // Determine whether we will need to map the memory of the new buffer.
    bool needs_mapped_ptr = false;
    if (memory_property_flags & vk::MemoryPropertyFlagBits::eHostVisible) {
      // We don't currently provide an interface for flushing mapped data, so
      // ensure that the allocated memory is cache-coherent.  This is more
      // convenient anyway.
      memory_property_flags |= vk::MemoryPropertyFlagBits::eHostCoherent;
      needs_mapped_ptr = true;
    }

    // Create buffer.
    vk::BufferCreateInfo buffer_create_info;
    buffer_create_info.size = size;
    buffer_create_info.usage = usage_flags;
    buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
    buffer_ = ESCHER_CHECKED_VK_RESULT(device.createBuffer(buffer_create_info));

    // Allocate memory and bind it to the buffer.
    mem_ = allocator->Allocate(device.getBufferMemoryRequirements(buffer_),
                               memory_property_flags);
    device.bindBufferMemory(buffer_, mem_->base(), mem_->offset());

    if (needs_mapped_ptr) {
      auto ptr = ESCHER_CHECKED_VK_RESULT(
          device.mapMemory(mem_->base(), mem_->offset(), mem_->size()));
      mapped_ptr_ = reinterpret_cast<uint8_t*>(ptr);
    }
  }

  ~UnownedBufferInfo() {
    if (mapped_ptr_) {
      // We currently assume that there is one Vulkan allocation per GpuMem.
      // If this is false, then this will potentially unmap the memory of other
      // buffers and images.
      FTL_CHECK(mem_->offset() == 0);
      device_.unmapMemory(mem_->base());
    }
    device_.destroyBuffer(buffer_);
  }

  vk::Buffer GetBuffer() override { return buffer_; }
  vk::DeviceSize GetSize() override { return size_; }
  uint8_t* GetMappedPointer() override { return mapped_ptr_; }

 private:
  vk::Device device_;
  vk::DeviceSize size_;
  vk::Buffer buffer_;
  impl::GpuMemPtr mem_;
  uint8_t* mapped_ptr_;
};

}  // namespace

BufferPtr BufferOwner::NewBuffer(std::unique_ptr<BufferInfo> info) {
  return ftl::AdoptRef(new Buffer(std::move(info), this));
}

Buffer::Buffer(vk::Device device,
               impl::GpuAllocator* allocator,
               vk::DeviceSize size,
               vk::BufferUsageFlags usage_flags,
               vk::MemoryPropertyFlags memory_property_flags)
    : Resource(nullptr),
      info_(std::make_unique<UnownedBufferInfo>(device,
                                                allocator,
                                                size,
                                                usage_flags,
                                                memory_property_flags)),
      buffer_(info_->GetBuffer()),
      size_(info_->GetSize()),
      ptr_(info_->GetMappedPointer()),
      owner_(nullptr) {}

Buffer::Buffer(std::unique_ptr<BufferInfo> info, BufferOwner* owner)
    : Resource(nullptr),
      info_(std::move(info)),
      buffer_(info_->GetBuffer()),
      size_(info_->GetSize()),
      ptr_(info_->GetMappedPointer()),
      owner_(owner) {
  FTL_DCHECK(owner);
}

Buffer::~Buffer() {
  if (owner_) {
    owner_->RecycleBuffer(std::move(info_));
  }
}

}  // namespace escher
