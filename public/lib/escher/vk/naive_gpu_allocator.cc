// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/naive_gpu_allocator.h"

namespace escher {

NaiveGpuAllocator::NaiveGpuAllocator(const VulkanContext& context)
    : GpuAllocator(context) {}

GpuMemPtr NaiveGpuAllocator::Allocate(vk::MemoryRequirements reqs,
                                      vk::MemoryPropertyFlags flags) {
  // TODO: need to manually overallocate and adjust offset to ensure alignment,
  // based on the content of reqs.alignment?  Probably not, but should verify.

  // More sophisticated subclasses of GpuAllocator will perform multiple
  // suballocations within a single GpuMemSlab; these will retain the unique_ptr
  // to the slab, along with additional metadata to track which regions within
  // it are available.  However, since we know that there is a 1-1 mapping, we
  // release our unique_ptr to the slab, since it is guaranteed to be returned
  // to us by FreeMem.
  return AllocateSlab(reqs, flags);
}

void NaiveGpuAllocator::OnSuballocationDestroyed(GpuMem* slab,
                                                 vk::DeviceSize size,
                                                 vk::DeviceSize offset) {}

}  // namespace escher
