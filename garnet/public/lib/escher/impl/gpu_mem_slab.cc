// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/gpu_mem_slab.h"

#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/naive_gpu_allocator.h"
#include "src/lib/fxl/logging.h"

namespace {

uint8_t* GetMappedPtr(vk::Device device, vk::DeviceMemory base,
                      vk::DeviceSize size) {
  TRACE_DURATION("gfx", "escher::GpuMemSlab::New[map]");
  auto ptr = escher::ESCHER_CHECKED_VK_RESULT(device.mapMemory(base, 0, size));
  return reinterpret_cast<uint8_t*>(ptr);
}

}  // namespace

namespace escher {
namespace impl {

GpuMemSlab::GpuMemSlab(vk::Device device, vk::DeviceMemory base,
                       vk::DeviceSize size, bool needs_mapped_ptr,
                       NaiveGpuAllocator* allocator)
    : GpuMem(base, size, 0,
             needs_mapped_ptr ? GetMappedPtr(device, base, size) : nullptr),
      device_(device),
      allocator_(allocator) {
  if (allocator_) {
    allocator_->OnSlabCreated(this->size());
  }
}

GpuMemSlab::~GpuMemSlab() {
  if (device_ && base()) {
    if (mapped_ptr()) {
      device_.unmapMemory(base());
    }
    device_.freeMemory(base());
  }
  if (allocator_) {
    allocator_->OnSlabDestroyed(size());
  }
}

}  // namespace impl
}  // namespace escher
