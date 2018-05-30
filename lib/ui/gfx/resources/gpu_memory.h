// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_GPU_MEMORY_H_
#define GARNET_LIB_UI_GFX_RESOURCES_GPU_MEMORY_H_

#include <vulkan/vulkan.hpp>

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include "garnet/lib/ui/gfx/resources/memory.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "lib/escher/vk/gpu_mem.h"

namespace scenic {
namespace gfx {

class GpuMemory;
using GpuMemoryPtr = fxl::RefPtr<GpuMemory>;

// Wraps Vulkan memory (VkDeviceMemory).
class GpuMemory : public Memory {
 public:
  static const ResourceTypeInfo kTypeInfo;

  GpuMemory(Session* session, scenic::ResourceId id, vk::Device device,
            vk::DeviceMemory mem, vk::DeviceSize size,
            uint32_t memory_type_index);

  // Helper method for creating GpuMemory object from a
  // ::fuchsia::ui::gfx::Memory. Create a GpuMemory resource object from a VMO
  // that represents a VkDeviceMemory. Releases the VMO.
  //
  // Returns the created GpuMemory object or nullptr if there was an error.
  static GpuMemoryPtr New(Session* session, scenic::ResourceId id,
                          vk::Device device, zx::vmo vmo,
                          ErrorReporter* error_reporter);

  // Helper method that calls the above method with the VMO from |args|. Also
  // checks the memory type in debug mode.
  static GpuMemoryPtr New(Session* session, scenic::ResourceId id,
                          vk::Device device,
                          ::fuchsia::ui::gfx::MemoryArgs args,
                          ErrorReporter* error_reporter);

  void Accept(class ResourceVisitor* visitor) override;

  const escher::GpuMemPtr& escher_gpu_mem() const { return escher_gpu_mem_; }

  vk::DeviceSize size() const { return escher_gpu_mem_->size(); }

 private:
  escher::GpuMemPtr escher_gpu_mem_ = nullptr;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_GPU_MEMORY_H_
