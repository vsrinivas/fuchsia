// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/impl/gpu_allocator.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {

// Convenient wrapper for vk::Buffer that provides automatic memory management.
// TODO: this is deprecated... all usages should be removed ASAP.
class BufferOLD {
 public:
  BufferOLD(vk::Device device,
            GpuAllocator* allocator,
            vk::DeviceSize size,
            vk::BufferUsageFlags usage_flags,
            vk::MemoryPropertyFlags memory_property_flags);
  BufferOLD(BufferOLD&& other);
  ~BufferOLD();

  vk::DeviceSize GetSize() const { return mem_->size(); }
  vk::Buffer buffer() const { return buffer_; }

  uint8_t* Map();
  void Unmap();

 private:
  vk::Device device_;
  GpuAllocator* allocator_;
  vk::BufferUsageFlags usage_flags_;
  vk::MemoryPropertyFlags memory_property_flags_;
  vk::Buffer buffer_;
  GpuMemPtr mem_;
  void* mapped_ = nullptr;

  FTL_DISALLOW_COPY_AND_ASSIGN(BufferOLD);
};

}  // namespace impl
}  // namespace escher
