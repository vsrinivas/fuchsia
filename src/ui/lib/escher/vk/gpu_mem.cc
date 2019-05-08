// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/gpu_mem.h"

#include "src/ui/lib/escher/impl/gpu_mem_slab.h"
#include "src/ui/lib/escher/impl/gpu_mem_suballocation.h"

namespace escher {

GpuMem::GpuMem(vk::DeviceMemory base, vk::DeviceSize size,
               vk::DeviceSize offset, uint8_t* mapped_ptr)
    : base_(base), size_(size), offset_(offset), mapped_ptr_(mapped_ptr) {}

GpuMem::~GpuMem() {}

GpuMemPtr GpuMem::AdoptVkMemory(vk::Device device, vk::DeviceMemory mem,
                                vk::DeviceSize size, bool needs_mapped_ptr) {
  return fxl::AdoptRef(
      new impl::GpuMemSlab(device, mem, size, needs_mapped_ptr, nullptr));
}

GpuMemPtr GpuMem::Suballocate(vk::DeviceSize size, vk::DeviceSize offset) {
  if (offset + size > size_) {
    return GpuMemPtr();
  }
  return fxl::AdoptRef(
      new impl::GpuMemSuballocation(GpuMemPtr(this), size, offset));
}

}  // namespace escher
