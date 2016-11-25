// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/vk/buffer.h"

namespace escher {
namespace impl {

class GpuUploader : BufferOwner {
 public:
  GpuUploader(CommandBufferPool* command_buffer_pool, GpuAllocator* allocator);
  ~GpuUploader();

  // Provides a pointer in host-accessible GPU memory, and methods to copy this
  // memory into optimally-formatted Images and Buffers.  Once all image/buffer
  // writes have been specified, call Submit().
  class Writer {
   public:
    Writer(Writer&& writer);
    ~Writer();

    // Schedule a buffer-to-buffer copy that will be submitted when Submit()
    // is called.  Retains a reference to the target until the submission's
    // CommandBuffer is retired.
    void WriteBuffer(const BufferPtr& target,
                     vk::BufferCopy region,
                     SemaphorePtr semaphore);

    // Schedule a buffer-to-image copy that will be submitted when Submit()
    // is called.  Retains a reference to the target until the submission's
    // CommandBuffer is retired.
    void WriteImage(const ImagePtr& target,
                    vk::BufferImageCopy region,
                    SemaphorePtr semaphore);

    // Submit all image/buffer writes that been made on this Writer.  It is an
    // error to call this more than once.
    void Submit();

    uint8_t* ptr() const { return ptr_; }
    vk::DeviceSize size() const { return size_; }

   private:
    // Constructor called by GpuUploader.
    friend class GpuUploader;
    Writer(BufferPtr buffer,
           CommandBuffer* command_buffer,
           vk::Queue queue,
           vk::DeviceSize size,
           vk::DeviceSize offset);

    // Add the resource to command_buffer_.  If semaphore is not null, set it as
    // target's wait-sempahore, and signal it when the command-buffer is
    // retired.
    void RememberTarget(ResourcePtr target, SemaphorePtr semaphore);

    BufferPtr buffer_;
    CommandBuffer* command_buffer_;
    vk::Queue queue_;
    vk::DeviceSize size_;
    vk::DeviceSize offset_;
    uint8_t* ptr_;
    bool has_writes_;

    FTL_DISALLOW_COPY_AND_ASSIGN(Writer);
  };

  // Get a Writer that has the specified amount of scratch space.
  Writer GetWriter(size_t size);

 private:
  // Implement BufferOwner::RecycleBuffer().
  void RecycleBuffer(std::unique_ptr<BufferInfo> info) override;

  // Item in free_buffers_.
  class TransferBufferInfo : public BufferInfo {
   public:
    TransferBufferInfo(vk::Buffer buffer,
                       vk::DeviceSize size,
                       uint8_t* ptr,
                       GpuMemPtr mem);
    ~TransferBufferInfo();

    vk::Buffer GetBuffer() override;
    vk::DeviceSize GetSize() override;
    uint8_t* GetMappedPointer() override;

    // Not part of public BufferInfo interface.  Called by GpuUploader.
    void DestroyBuffer(vk::Device device);

    vk::Buffer buffer;
    vk::DeviceSize size;
    uint8_t* ptr;
    GpuMemPtr mem;
  };

  // If current_buffer_ doesn't have enough room, release it and prepare a
  // suitable buffer.
  void PrepareForWriterOfSize(vk::DeviceSize size);

  // Vends command-buffers to be submitted on queue_.
  CommandBufferPool* command_buffer_pool_;

  vk::Device device_;

  // Use the queue transfer-specfic queue if available, otherwise the default
  // queue.
  vk::Queue queue_;

  // Used to allocate backing memory for the pool's buffers.
  GpuAllocator* allocator_;

  // List of free buffers that are available for allocation.
  std::vector<std::unique_ptr<BufferInfo>> free_buffers_;

  // The buffer that is currently being used for writes.
  BufferPtr current_buffer_;

  // The offset within the buffer that will be used for the next created Writer.
  vk::DeviceSize current_offset_;
  // Number of currently-existing TransferBufferInfo objects that were created
  // by this uploader.
  uint32_t allocation_count_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuUploader);
};

}  // namespace impl
}  // namespace escher
