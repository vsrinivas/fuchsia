// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_pool.h"

#include <src/lib/fxl/logging.h>

void BufferPool::AddBuffer(const CodecBuffer* buffer) {
  free_buffers_.Push(buffer);
}

std::optional<const CodecBuffer*> BufferPool::AllocateBuffer(size_t alloc_len) {
  auto maybe_buffer = free_buffers_.WaitForElement();
  if (!maybe_buffer) {
    return std::nullopt;
  }
  auto& buffer = *maybe_buffer;

  {
    std::lock_guard<std::mutex> lock(lock_);
    buffers_in_use_[buffer->buffer_base()] = {
        .buffer = buffer,
        .bytes_used = alloc_len,
    };
  }

  return maybe_buffer;
}

void BufferPool::FreeBuffer(uint8_t* base) {
  BufferPool::Allocation allocation;
  fit::closure free_callback;
  {
    std::lock_guard<std::mutex> lock(lock_);
    auto iter = buffers_in_use_.find(base);
    ZX_DEBUG_ASSERT(iter != buffers_in_use_.end());
    allocation = iter->second;
    buffers_in_use_.erase(base);
  }
  free_buffers_.Push(std::move(allocation.buffer));
}

std::optional<BufferPool::Allocation> BufferPool::FindBufferByBase(
    uint8_t* base) {
  std::lock_guard lock(lock_);
  auto iter = buffers_in_use_.find(base);
  if (iter == buffers_in_use_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

void BufferPool::Reset(bool keep_data) { free_buffers_.Reset(keep_data); }

void BufferPool::StopAllWaits() { free_buffers_.StopAllWaits(); }

bool BufferPool::has_buffers_in_use() {
  std::lock_guard<std::mutex> lock(lock_);
  return !buffers_in_use_.empty();
}
