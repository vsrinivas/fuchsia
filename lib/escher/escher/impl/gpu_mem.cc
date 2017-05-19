// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/gpu_mem.h"
#include "escher/impl/gpu_mem_slab.h"

namespace escher {
namespace impl {

// Helper class for GpuMem::Allocate(), which returns an instance of this class.
// When the instance is destroyed, it notifies the GpuMem that it was allocated
// from.
class GpuMemSuballocation : public GpuMem {
 public:
  GpuMemSuballocation(GpuMemPtr mem, vk::DeviceSize offset, vk::DeviceSize size)
      : GpuMem(mem->base(), mem->offset() + offset, size),
        mem_(std::move(mem)) {}

  ~GpuMemSuballocation() { mem_->OnAllocationDestroyed(size(), offset()); }

 private:
  // The memory that this was sub-allocated from.
  GpuMemPtr mem_;
};

GpuMem::GpuMem(vk::DeviceMemory base,
               vk::DeviceSize size,
               vk::DeviceSize offset)
    : base_(base), size_(size), offset_(offset) {}

GpuMem::~GpuMem() {}

GpuMemPtr GpuMem::New(vk::Device device,
                      vk::PhysicalDevice physical_device,
                      vk::MemoryRequirements reqs,
                      vk::MemoryPropertyFlags flags) {
  return GpuMemSlab::New(device, physical_device, reqs, flags, nullptr);
}

GpuMemPtr GpuMem::New(vk::Device device,
                      vk::DeviceMemory mem,
                      vk::DeviceSize size,
                      uint32_t memory_type_index) {
  return ftl::AdoptRef(new GpuMemSlab(device, mem, size, memory_type_index));
}

GpuMemPtr GpuMem::Allocate(vk::DeviceSize size, vk::DeviceSize offset) {
  if (offset + size > size_) {
    return GpuMemPtr();
  }
  return ftl::MakeRefCounted<GpuMemSuballocation>(GpuMemPtr(this), size,
                                                  offset_ + offset);
}

}  // namespace impl
}  // namespace escher
