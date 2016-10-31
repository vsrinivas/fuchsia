// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/gpu_mem_slab.h"

#include "escher/impl/gpu_allocator.h"
#include "ftl/logging.h"

namespace escher {
namespace impl {

GpuMemSlab::GpuMemSlab(vk::DeviceMemory base,
                       vk::DeviceSize size,
                       uint32_t memory_type_index,
                       GpuAllocator* allocator)
    : base_(base),
      size_(size),
      memory_type_index_(memory_type_index),
      allocator_(allocator),
      ref_count_(0) {}

GpuMemSlab::~GpuMemSlab() {
  FTL_CHECK(ref_count_ == 0);
}

void GpuMemSlab::AddRef() {
  ++ref_count_;
}

void GpuMemSlab::FreeMem(vk::DeviceSize offset, vk::DeviceSize size) {
  FTL_DCHECK(ref_count_ >= 1);
  allocator_->FreeMem(this, --ref_count_, offset, size);
}

}  // namespace impl
}  // namespace escher
