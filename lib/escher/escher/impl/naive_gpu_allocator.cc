// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/naive_gpu_allocator.h"

#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

NaiveGpuAllocator::NaiveGpuAllocator(const VulkanContext& context)
    : GpuAllocator(context) {}

GpuMemPtr NaiveGpuAllocator::Allocate(vk::MemoryRequirements reqs,
                                      vk::MemoryPropertyFlags flags) {
  // TODO: cache flags for efficiency? Or perhaps change signature of this
  // method to directly take the memory-type index.
  auto memory_type_index =
      GetMemoryTypeIndex(physical_device(), reqs.memoryTypeBits, flags);

  // TODO: need to manually overallocate and adjust offset to ensure alignment,
  // based on the content of reqs.alignment?  Probably not, but should verify.

  // More sophisticated subclasses of GpuAllocator will perform multiple
  // suballocations within a single GpuMemSlab; these will retain the unique_ptr
  // to the slab, along with additional metadata to track which regions within
  // it are available.  However, since we know that there is a 1-1 mapping, we
  // release our unique_ptr to the slab, since it is guaranteed to be returned
  // to us by FreeMem.
  auto slab = AllocateSlab(reqs.size, memory_type_index);
  return AllocateMem(slab.release(), 0, reqs.size);
}

void NaiveGpuAllocator::FreeMem(GpuMemSlab* slab,
                                uint32_t slab_ref_count,
                                vk::DeviceSize offset,
                                vk::DeviceSize size) {
  FTL_DCHECK(slab_ref_count == 0);
  FTL_DCHECK(offset == 0);
  FTL_DCHECK(size == slab->size());

  // See comment in Allocate() to see why it is OK to create a unique_ptr here.
  FreeSlab(std::unique_ptr<GpuMemSlab>(slab));
}

}  // namespace impl
}  // namespace escher
