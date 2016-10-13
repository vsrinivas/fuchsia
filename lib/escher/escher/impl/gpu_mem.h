// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "ftl/macros.h"

namespace escher {
namespace impl {

class GpuAllocator;

// Memory allocated by GpuAllocator.
class GpuMem {
 public:
  // Equivalent to nullptr.
  GpuMem();
  // Called by GpuAllocator::Allocate().
  GpuMem(vk::DeviceMemory base,
         vk::DeviceSize offset,
         vk::DeviceSize size,
         uint32_t memory_type_index,
         GpuAllocator* allocator);
  GpuMem(GpuMem&& other);
  ~GpuMem();

  vk::DeviceMemory base() const { return base_; }
  vk::DeviceSize offset() const { return offset_; }
  vk::DeviceSize size() const { return size_; }
  uint32_t memory_type_index() const { return memory_type_index_; }

 private:
  vk::DeviceMemory base_;
  vk::DeviceSize offset_;
  vk::DeviceSize size_;
  uint32_t memory_type_index_;
  GpuAllocator* allocator_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuMem);
};

}  // namespace impl
}  // namespace escher
