// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/gpu_uploader.h"

#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/gpu_allocator.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/image.h"

#include <algorithm>

namespace escher {
namespace impl {

GpuUploader::Writer::Writer(BufferPtr buffer,
                            CommandBuffer* command_buffer,
                            vk::Queue queue,
                            vk::DeviceSize size,
                            vk::DeviceSize offset)
    : buffer_(std::move(buffer)),
      command_buffer_(command_buffer),
      queue_(queue),
      size_(size),
      offset_(offset),
      ptr_(buffer_->ptr() + offset_),
      has_writes_(false) {
  FTL_DCHECK(buffer_ && command_buffer_ && queue_ && ptr_);
}

GpuUploader::Writer::Writer(Writer&& other)
    : buffer_(std::move(other.buffer_)),
      command_buffer_(other.command_buffer_),
      queue_(other.queue_),
      size_(other.size_),
      offset_(other.offset_),
      ptr_(other.ptr_),
      has_writes_(other.has_writes_) {
  other.size_ = 0;
  other.offset_ = 0;
  other.command_buffer_ = nullptr;
  other.queue_ = nullptr;
  other.ptr_ = nullptr;
  other.has_writes_ = false;
}

void GpuUploader::Writer::Submit() {
  FTL_CHECK(command_buffer_);
  if (has_writes_) {
    if (has_writes_) {
      command_buffer_->AddUsedResource(std::move(buffer_));
      command_buffer_->Submit(queue_, nullptr);
    } else {
      // We need to submit the buffer anyway, otherwise we'll stall the
      // CommandPool.
      FTL_DLOG(WARNING) << "Submitting command-buffer without any writes.";
      command_buffer_->Submit(queue_, nullptr);
    }
  }
  buffer_ = nullptr;
  command_buffer_ = nullptr;
  queue_ = nullptr;
  size_ = 0;
  offset_ = 0;
  ptr_ = 0;
  has_writes_ = false;
}

GpuUploader::Writer::~Writer() {
  FTL_CHECK(!command_buffer_);
}

void GpuUploader::Writer::WriteBuffer(const BufferPtr& target,
                                      vk::BufferCopy region,
                                      SemaphorePtr semaphore) {
  has_writes_ = true;
  region.srcOffset += offset_;
  RememberTarget(target, std::move(semaphore));

  command_buffer_->get().copyBuffer(buffer_->get(), target->get(), 1, &region);
}

void GpuUploader::Writer::WriteImage(const ImagePtr& target,
                                     vk::BufferImageCopy region,
                                     SemaphorePtr semaphore) {
  has_writes_ = true;
  region.bufferOffset += offset_;
  RememberTarget(target, std::move(semaphore));

  command_buffer_->TransitionImageLayout(target, vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eTransferDstOptimal);
  command_buffer_->get().copyBufferToImage(buffer_->get(), target->get(),
                                           vk::ImageLayout::eTransferDstOptimal,
                                           1, &region);
  command_buffer_->TransitionImageLayout(
      target, vk::ImageLayout::eTransferDstOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal);
}

void GpuUploader::Writer::RememberTarget(ResourcePtr target,
                                         SemaphorePtr semaphore) {
  if (semaphore) {
    target->SetWaitSemaphore(semaphore);
    command_buffer_->AddSignalSemaphore(std::move(semaphore));
  }
  command_buffer_->AddUsedResource(std::move(target));
}

GpuUploader::TransferBufferInfo::TransferBufferInfo(vk::Buffer buf,
                                                    vk::DeviceSize sz,
                                                    uint8_t* p,
                                                    GpuMemPtr m)
    : buffer(buf), size(sz), ptr(p), mem(m) {}

GpuUploader::TransferBufferInfo::~TransferBufferInfo() {
  // GpuUploader is responsible for destroying the buffer.
  FTL_DCHECK(!buffer);
}

vk::Buffer GpuUploader::TransferBufferInfo::GetBuffer() {
  return buffer;
}

vk::DeviceSize GpuUploader::TransferBufferInfo::GetSize() {
  return size;
}

uint8_t* GpuUploader::TransferBufferInfo::GetMappedPointer() {
  return ptr;
}

GpuUploader::GpuUploader(CommandBufferPool* command_buffer_pool,
                         GpuAllocator* allocator)
    : command_buffer_pool_(command_buffer_pool),
      device_(command_buffer_pool_->device()),
      queue_(command_buffer_pool_->queue()),
      allocator_(allocator),
      current_offset_(0),
      allocation_count_(0) {}

GpuUploader::~GpuUploader() {
  FTL_DCHECK(allocation_count_ == 0);
}

GpuUploader::Writer GpuUploader::GetWriter(size_t s) {
  vk::DeviceSize size = s;
  FTL_DCHECK(size == s);
  PrepareForWriterOfSize(size);
  Writer writer(current_buffer_, command_buffer_pool_->GetCommandBuffer(),
                queue_, size, current_offset_);
  current_offset_ += size;

  // Not all clients will require this alignment, but let's be safe for now.
  constexpr vk::DeviceSize kAlignment = 16;
  vk::DeviceSize adjustment = kAlignment - (current_offset_ % kAlignment);
  if (adjustment != kAlignment) {
    current_offset_ += adjustment;
  }

  return writer;
}

void GpuUploader::RecycleBuffer(std::unique_ptr<BufferInfo> info) {
  free_buffers_.push_back(std::move(info));
}

void GpuUploader::PrepareForWriterOfSize(vk::DeviceSize size) {
  if (current_buffer_ && current_buffer_->size() >= current_offset_ + size) {
    // There is enough space in the current buffer.
    return;
  }
  current_buffer_ = nullptr;
  current_offset_ = 0;

  // Try to find a large-enough buffer in the free-list.
  while (!free_buffers_.empty()) {
    auto info = std::move(free_buffers_.back());
    free_buffers_.pop_back();
    if (info->GetSize() >= size) {
      current_buffer_ = NewBuffer(std::move(info));
      return;
    }
    // Destroy buffer if it is too small.
    --allocation_count_;
  }

  // No large-enough buffer was found, so create a new one.  Make it big enough
  // for at least two writes of the requested size.
  constexpr vk::DeviceSize kMinBufferSize = 1024 * 1024;
  constexpr vk::DeviceSize kOverAllocationFactor = 2;
  size = std::max(kMinBufferSize, size * kOverAllocationFactor);
  vk::Buffer buffer;
  {
    vk::BufferCreateInfo buffer_create_info;
    buffer_create_info.size = size;
    buffer_create_info.usage = vk::BufferUsageFlagBits::eTransferSrc;
    buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
    buffer = ESCHER_CHECKED_VK_RESULT(device_.createBuffer(buffer_create_info));
  }
  // Allocate memory and bind it to the buffer.
  auto memory_properties = vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent;
  GpuMemPtr mem = allocator_->Allocate(
      device_.getBufferMemoryRequirements(buffer), memory_properties);
  device_.bindBufferMemory(buffer, mem->base(), mem->offset());
  void* ptr = ESCHER_CHECKED_VK_RESULT(
      device_.mapMemory(mem->base(), mem->offset(), mem->size()));
  // Wrap everything in a TransferBufferInfo, and wrap that in a Buffer.
  current_buffer_ = NewBuffer(std::make_unique<TransferBufferInfo>(
      buffer, size, reinterpret_cast<uint8_t*>(ptr), std::move(mem)));
  ++allocation_count_;
}

}  // namespace impl
}  // namespace escher
