// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_GPU_ALLOCATOR_H_
#define SRC_UI_LIB_ESCHER_VK_GPU_ALLOCATOR_H_

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/vk/buffer.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/ui/lib/escher/vk/image.h"

#include <vulkan/vulkan.hpp>

namespace escher {

// GpuAllocator is an interface for allocating vulkan-aware blocks of memory,
// and objects that are backed by said memory (i.e., buffers and images).
//
// Vulkan implementations are not required to support large numbers of raw
// memory allocations.  Instead, applications are expected to allocate larger
// chunks of memory, and sub-allocate from within these chunks.
//
// GpuAllocator defines the interface that clients use to obtain
// sub-allocated memory; the specific sub-allocation strategy employed is
// the responsibility of concrete subclasses of GpuAllocator.
class GpuAllocator {
 public:
  GpuAllocator() : weak_factory_(this) {}
  virtual ~GpuAllocator() = default;

  fxl::WeakPtr<GpuAllocator> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  virtual GpuMemPtr AllocateMemory(vk::MemoryRequirements reqs,
                                   vk::MemoryPropertyFlags memory_property_flags) = 0;

  // If |out_ptr| is non-null, this buffer must be backed by a dedicated
  // piece of memory (i.e.,
  // VkMemoryDedicatedRequirements.requiresDedicatedAllocation
  // == true). That memory must be accessible through the GpuMem returned in
  // |out_ptr|.
  virtual BufferPtr AllocateBuffer(ResourceManager* manager, vk::DeviceSize size,
                                   vk::BufferUsageFlags usage_flags,
                                   vk::MemoryPropertyFlags memory_property_flags,
                                   GpuMemPtr* out_ptr = nullptr) = 0;

  virtual ImagePtr AllocateImage(ResourceManager* manager, const escher::ImageInfo& info,
                                 GpuMemPtr* out_ptr = nullptr) = 0;

  virtual size_t GetTotalBytesAllocated() const = 0;
  virtual size_t GetUnusedBytesAllocated() const = 0;

  fxl::WeakPtrFactory<GpuAllocator> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(GpuAllocator);
};

typedef fxl::WeakPtr<GpuAllocator> GpuAllocatorWeakPtr;

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_GPU_ALLOCATOR_H_
