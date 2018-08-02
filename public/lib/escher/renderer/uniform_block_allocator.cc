// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/uniform_block_allocator.h"

#include "lib/escher/impl/uniform_buffer_pool.h"
#include "lib/escher/util/align.h"
#include "lib/escher/vk/buffer.h"

namespace escher {

UniformBlockAllocator::UniformBlockAllocator(
    impl::UniformBufferPoolWeakPtr pool)
    : pool_(std::move(pool)), buffer_size_(pool_->buffer_size()) {
  Reset();
}

UniformAllocation UniformBlockAllocator::Allocate(size_t size,
                                                  size_t alignment) {
  write_index_ = AlignedToNext(write_index_, alignment);
  if (write_index_ + size > buffer_size_) {
    buffers_.push_back(pool_->Allocate());
    FXL_DCHECK(buffers_.back()->host_ptr());
    write_index_ = 0;
  }
  auto& buf = buffers_.back();
  UniformAllocation allocation = {.buffer = buf.get(),
                                  .offset = write_index_,
                                  .size = size,
                                  .host_ptr = buf->host_ptr() + write_index_};
  write_index_ += size;
  return allocation;
}

void UniformBlockAllocator::Reset() {
  // This guarantees that the next allocation attempt will obtain a new
  // suballocation buffer.
  write_index_ = buffer_size_;

  // Clear the current contents of the buffer, and reserve enough space to hold
  // about a megabyte's worth of Buffers.
  constexpr size_t kBufferVectCapacity = 16;
  buffers_.clear();
  buffers_.reserve(kBufferVectCapacity);
}

std::vector<BufferPtr> UniformBlockAllocator::TakeBuffers() {
  std::vector<BufferPtr> retval(std::move(buffers_));
  Reset();
  return retval;
}

}  // namespace escher
