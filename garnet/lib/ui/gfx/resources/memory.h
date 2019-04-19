// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_MEMORY_H_
#define GARNET_LIB_UI_GFX_RESOURCES_MEMORY_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <vulkan/vulkan.hpp>

#include "garnet/lib/ui/gfx/resources/resource.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "lib/escher/vk/gpu_mem.h"
#include "lib/fsl/vmo/shared_vmo.h"

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

  static MemoryPtr New(Session* session, ResourceId id,
                       ::fuchsia::ui::gfx::MemoryArgs args,
                       ErrorReporter* error_reporter);

  // TODO(SCN-1012): Temporary solution to determine which image class to use.
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
  const escher::GpuMemPtr& GetGpuMem() {
    // TODO(SCN-999): Passive lazy instantiation may not be ideal, either from a
    // performance standpoint, or from an external logic standpoint. Consider
    // acquire/release semantics. This would also map well to vkCopyBuffer
    // commands and shadow buffers.
    if (!escher_gpu_mem_) {
      escher_gpu_mem_ = ImportGpuMemory();
    }
    return escher_gpu_mem_;
  }

  // |Resource|
  void Accept(ResourceVisitor* visitor) override;

 private:
  Memory(Session* session, ResourceId id, ::fuchsia::ui::gfx::MemoryArgs args);

  escher::GpuMemPtr ImportGpuMemory();
  zx::vmo DuplicateVmo();

  bool is_host_;
  fxl::RefPtr<fsl::SharedVmo> shared_vmo_;
  uint64_t allocation_size_;
  escher::GpuMemPtr escher_gpu_mem_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_MEMORY_H_
