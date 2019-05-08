// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_IMPL_GPU_MEM_SLAB_H_
#define SRC_UI_LIB_ESCHER_IMPL_GPU_MEM_SLAB_H_

#include <vulkan/vulkan.hpp>

#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/lib/fxl/macros.h"

namespace escher {

class NaiveGpuAllocator;

namespace impl {

class GpuMemSlab;
typedef fxl::RefPtr<GpuMemSlab> GpuMemSlabPtr;

// GpuMemSlab represents a single Vulkan memory allocation, created directly
// through a vkDevice. It should only be created via specific subclasses of
// GpuAllocator, or through GpuMem::AdoptMemory when transferring ownership of a
// existing vk::DeviceMemory object.
class GpuMemSlab final : public GpuMem {
 public:
  ~GpuMemSlab() override;

 private:
  // Instances of GpuMemSlab may only be created by
  // NaiveGpuAllocator::AllocateMemory() and GpuMem::AdoptMemory.
  friend class ::escher::NaiveGpuAllocator;
  friend class ::escher::GpuMem;
  GpuMemSlab(vk::Device device, vk::DeviceMemory base, vk::DeviceSize size,
             bool needs_mapped_ptr, NaiveGpuAllocator* allocator);

  vk::Device device_;
  NaiveGpuAllocator* allocator_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GpuMemSlab);
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_IMPL_GPU_MEM_SLAB_H_
