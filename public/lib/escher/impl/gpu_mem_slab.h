// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_GPU_MEM_SLAB_H_
#define LIB_ESCHER_IMPL_GPU_MEM_SLAB_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/vk/gpu_mem.h"
#include "lib/fxl/macros.h"

namespace escher {

class GpuAllocator;

namespace impl {

class GpuMemSlab;
typedef fxl::RefPtr<GpuMemSlab> GpuMemSlabPtr;

// GpuMemSlab represents a single Vulkan memory allocation, which a GpuAllocator
// may use to sub-allocate multiple GpuMem instances.  It can only be created
// via GpuMem::New() and GpuAllocator::AllocateSlab().  See class comment of
// GpuAllocator for more details.
class GpuMemSlab final : public GpuMem {
 public:
  uint32_t memory_type_index() const { return memory_type_index_; }

  ~GpuMemSlab() override;

 private:
  // Instances of GpuMemSlab may only be created by GpuAllocator::AllocateSlab()
  // and GpuMem::New().
  friend class ::escher::GpuAllocator;
  friend class ::escher::GpuMem;
  static GpuMemSlabPtr New(vk::Device device,
                           vk::PhysicalDevice physical_device,
                           vk::MemoryRequirements reqs,
                           vk::MemoryPropertyFlags flags,
                           GpuAllocator* allocator);
  GpuMemSlab(vk::Device device, vk::DeviceMemory base, vk::DeviceSize size,
             uint8_t* mapped_ptr, uint32_t memory_type_index,
             GpuAllocator* allocator);

  void OnAllocationDestroyed(vk::DeviceSize size,
                             vk::DeviceSize offset) override;

  vk::Device device_;
  uint32_t memory_type_index_;
  GpuAllocator* allocator_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GpuMemSlab);
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_GPU_MEM_SLAB_H_
