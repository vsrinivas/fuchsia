// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/gpu_mem.h"

#include "escher/impl/gpu_allocator.h"

namespace escher {
namespace impl {

GpuMem::GpuMem(vk::DeviceMemory base,
               vk::DeviceSize offset,
               vk::DeviceSize size,
               uint32_t memory_type_index,
               GpuAllocator* allocator)
    : base_(base),
      offset_(offset),
      size_(size),
      memory_type_index_(memory_type_index),
      allocator_(allocator) {}

GpuMem::GpuMem(GpuMem&& other)
    : base_(other.base_),
      offset_(other.offset_),
      size_(other.size_),
      memory_type_index_(other.memory_type_index_),
      allocator_(other.allocator_) {
  other.base_ = 0;
  other.offset_ = 0;
  other.size_ = 0;
  other.memory_type_index_ = UINT32_MAX;
  other.allocator_ = nullptr;
}

GpuMem::~GpuMem() {
  // If this object has been moved, then destruction is a no-op.
  if (allocator_) {
    // Move allocator aside, so that when the moved version is destroyed, it
    // won't recursively ask the allocator to free it.
    GpuAllocator* saved = allocator_;
    allocator_ = nullptr;
    saved->Free(std::move(*this));
  }
}

}  // namespace impl
}  // namespace escher
