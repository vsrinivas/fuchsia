// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/gpu_mem.h"

#include "lib/escher/impl/gpu_mem_slab.h"
#include "lib/escher/impl/gpu_mem_suballocation.h"

namespace escher {

GpuMem::GpuMem(vk::DeviceMemory base, vk::DeviceSize size,
               vk::DeviceSize offset, uint8_t* mapped_ptr)
    : base_(base), size_(size), offset_(offset), mapped_ptr_(mapped_ptr) {}

GpuMem::~GpuMem() {}

GpuMemPtr GpuMem::New(vk::Device device, vk::PhysicalDevice physical_device,
                      vk::MemoryRequirements reqs,
                      vk::MemoryPropertyFlags flags) {
  return impl::GpuMemSlab::New(device, physical_device, reqs, flags, nullptr);
}

GpuMemPtr GpuMem::New(vk::Device device, vk::DeviceMemory mem,
                      vk::DeviceSize size, uint32_t memory_type_index) {
  return fxl::AdoptRef(new impl::GpuMemSlab(device, mem, size, nullptr,
                                            memory_type_index, nullptr));
}

GpuMemPtr GpuMem::Allocate(vk::DeviceSize size, vk::DeviceSize offset) {
  if (offset + size > size_) {
    return GpuMemPtr();
  }
  return fxl::AdoptRef(
      new impl::GpuMemSuballocation(GpuMemPtr(this), size, offset));
}

}  // namespace escher
