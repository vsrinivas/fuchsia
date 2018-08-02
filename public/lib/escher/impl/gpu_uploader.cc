// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/gpu_uploader.h"

#include <algorithm>

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/gpu_allocator.h"
#include "lib/escher/vk/image.h"

namespace escher {
namespace impl {

GpuUploader::Writer::Writer(BufferPtr buffer, CommandBuffer* command_buffer,
                            vk::Queue queue, vk::DeviceSize size,
                            vk::DeviceSize offset)
    : buffer_(std::move(buffer)),
      command_buffer_(command_buffer),
      queue_(queue),
      size_(size),
      offset_(offset),
      ptr_(buffer_->host_ptr() + offset_),
      has_writes_(false) {
  FXL_DCHECK(buffer_ && command_buffer_ && queue_ && ptr_);
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
  FXL_CHECK(command_buffer_);
  if (has_writes_) {
    if (has_writes_) {
      command_buffer_->KeepAlive(std::move(buffer_));
      command_buffer_->Submit(queue_, nullptr);
    } else {
      // We need to submit the buffer anyway, otherwise we'll stall the
      // CommandPool.
      FXL_DLOG(WARNING) << "Submitting command-buffer without any writes.";
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

GpuUploader::Writer::~Writer() { FXL_CHECK(!command_buffer_); }

void GpuUploader::Writer::WriteBuffer(const BufferPtr& target,
                                      vk::BufferCopy region,
                                      SemaphorePtr semaphore) {
  has_writes_ = true;
  region.srcOffset += offset_;
  if (semaphore) {
    target->SetWaitSemaphore(semaphore);
    command_buffer_->AddSignalSemaphore(std::move(semaphore));
  }
  command_buffer_->KeepAlive(target);
  command_buffer_->vk().copyBuffer(buffer_->vk(), target->vk(), 1, &region);
}

void GpuUploader::Writer::WriteImage(const ImagePtr& target,
                                     vk::BufferImageCopy region,
                                     SemaphorePtr semaphore) {
  has_writes_ = true;
  region.bufferOffset += offset_;

  command_buffer_->TransitionImageLayout(target, vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eTransferDstOptimal);
  command_buffer_->vk().copyBufferToImage(buffer_->vk(), target->vk(),
                                          vk::ImageLayout::eTransferDstOptimal,
                                          1, &region);
  command_buffer_->TransitionImageLayout(
      target, vk::ImageLayout::eTransferDstOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal);

  if (semaphore) {
    if (target->HasWaitSemaphore()) {
      target->ReplaceWaitSemaphore(semaphore);
    } else {
      target->SetWaitSemaphore(semaphore);
    }
    command_buffer_->AddSignalSemaphore(std::move(semaphore));
  }
  command_buffer_->KeepAlive(target);
}

GpuUploader::GpuUploader(EscherWeakPtr weak_escher,
                         CommandBufferPool* command_buffer_pool,
                         GpuAllocator* allocator)
    : ResourceRecycler(std::move(weak_escher)),
      command_buffer_pool_(command_buffer_pool
                               ? command_buffer_pool
                               : escher()->command_buffer_pool()),
      device_(command_buffer_pool_->device()),
      queue_(command_buffer_pool_->queue()),
      allocator_(allocator ? allocator : escher()->gpu_allocator()),
      current_offset_(0) {
  FXL_DCHECK(command_buffer_pool_);
  FXL_DCHECK(allocator_);
}

GpuUploader::~GpuUploader() { current_buffer_ = nullptr; }

GpuUploader::Writer GpuUploader::GetWriter(size_t s) {
  vk::DeviceSize size = s;
  FXL_DCHECK(size == s);
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

void GpuUploader::PrepareForWriterOfSize(vk::DeviceSize size) {
  if (current_buffer_ && current_buffer_->size() >= current_offset_ + size) {
    // There is enough space in the current buffer.
    return;
  }
  current_buffer_ = nullptr;
  current_offset_ = 0;

  // Try to find a large-enough buffer in the free-list.
  while (!free_buffers_.empty()) {
    std::unique_ptr<Buffer> buf(std::move(free_buffers_.back()));
    free_buffers_.pop_back();

    // Return buffer if it is big enough, otherwise destroy it and keep looking.
    if (buf->size() >= size) {
      current_buffer_ = BufferPtr(buf.release());
      return;
    }
  }

  // No large-enough buffer was found, so create a new one.  Make it big enough
  // for at least two writes of the requested size.
  constexpr vk::DeviceSize kMinBufferSize = 1024 * 1024;
  constexpr vk::DeviceSize kOverAllocationFactor = 2;
  size = std::max(kMinBufferSize, size * kOverAllocationFactor);
  auto memory_properties = vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent;
  current_buffer_ =
      Buffer::New(this, allocator_, size, vk::BufferUsageFlagBits::eTransferSrc,
                  memory_properties);
}

void GpuUploader::RecycleResource(std::unique_ptr<Resource> resource) {
  FXL_DCHECK(resource->IsKindOf<Buffer>());
  free_buffers_.emplace_back(static_cast<Buffer*>(resource.release()));
}

}  // namespace impl
}  // namespace escher
