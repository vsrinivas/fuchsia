// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_MEMORY_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_MEMORY_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>

#include "src/lib/fsl/vmo/shared_vmo.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"

#include <vulkan/vulkan.hpp>

namespace scenic_impl {
namespace gfx {

class Memory;
using MemoryPtr = fxl::RefPtr<Memory>;

// Memory is a resource that represents most forms of raw texture memory --
// gpu-bound, cpu-bound, and even shared-memory on UMA platforms. Since the use
// case for this memory is not known until well after object constuction, this
// class's primary job is to provide accessor methods and cached pointers for
// derivative objects, such as zx::vmos and escher::GpuMemPtr objects, that
// represent this memory having been mapped into CPU memory and
// vk::DeviceMemory, respectively.
class Memory : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  static MemoryPtr New(Session* session, ResourceId id, ::fuchsia::ui::gfx::MemoryArgs args,
                       ErrorReporter* error_reporter);
  static MemoryPtr New(Session* session, ResourceId id, zx::vmo vmo,
                       vk::MemoryAllocateInfo alloc_info, ErrorReporter* error_reporter);

  // TODO(fxbug.dev/24225): Temporary solution to determine which image class to use.
  // If image classes can depend on MemoryArgs, then this can become a real
  // solution once the MemoryArgs supports all formats. Alternatively, the Image
  // classes could be unified in a similar way that the Memory classes were
  // unified.
  bool is_host() const { return is_host_; }

  size_t size() const { return allocation_size_; }
  void* host_ptr() const {
    // SharedVMO already lazily maps in response to the first map request, so we
    // don't need additional logic here.
    return shared_vmo_->Map();
  }

  // |alloc_info| is an optional parameter. Caller can pass a specific struct or expect this class
  // to create vk::MemoryAllocateInfo from |shared_vmo_|.
  const escher::GpuMemPtr& GetGpuMem(ErrorReporter* reporter,
                                     vk::MemoryAllocateInfo* alloc_info = nullptr) {
    // TODO(fxbug.dev/24213): Passive lazy instantiation may not be ideal, either from a
    // performance standpoint, or from an external logic standpoint. Consider
    // acquire/release semantics. This would also map well to vkCopyBuffer
    // commands and shadow buffers.
    if (!escher_gpu_mem_) {
      escher_gpu_mem_ = ImportGpuMemory(reporter, alloc_info);
    }
    return escher_gpu_mem_;
  }

  // |Resource|
  void Accept(ResourceVisitor* visitor) override;

  // This function is used for tests, so they can easily detect if they should
  // bother trying to test UMA memory flows.
  static uint32_t HasSharedMemoryPools(vk::Device device, vk::PhysicalDevice physical_device);

 private:
  Memory(Session* session, ResourceId id, bool is_host, zx::vmo vmo, uint64_t allocation_size);

  escher::GpuMemPtr ImportGpuMemory(ErrorReporter* reporter, vk::MemoryAllocateInfo* alloc_info);

  const bool is_host_;
  const fxl::RefPtr<fsl::SharedVmo> shared_vmo_;
  const uint64_t allocation_size_;
  escher::GpuMemPtr escher_gpu_mem_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_MEMORY_H_
