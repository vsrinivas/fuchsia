// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "apps/mozart/src/scene_manager/resources/memory.h"
#include "apps/mozart/src/scene_manager/util/error_reporter.h"
#include "escher/vk/gpu_mem.h"

namespace scene_manager {

class GpuMemory;
using GpuMemoryPtr = ftl::RefPtr<GpuMemory>;

// Wraps Vulkan memory (VkDeviceMemory).
class GpuMemory : public Memory {
 public:
  static const ResourceTypeInfo kTypeInfo;

  GpuMemory(Session* session,
            scenic::ResourceId id,
            vk::Device device,
            vk::DeviceMemory mem,
            vk::DeviceSize size,
            uint32_t memory_type_index);

  // Helper method for creating GpuMemory object from a scenic::Memory.
  // Create a GpuMemory resource object from a VMO that represents a
  // VkDeviceMemory. Releases the VMO.
  //
  // Returns the created GpuMemory object or nullptr if there was an error.
  static GpuMemoryPtr New(Session* session,
                          scenic::ResourceId id,
                          vk::Device device,
                          mx::vmo vmo,
                          ErrorReporter* error_reporter);

  // Helper method that calls the above method with the VMO from |args|. Also
  // checks the memory type in debug mode.
  static GpuMemoryPtr New(Session* session,
                          scenic::ResourceId id,
                          vk::Device device,
                          const scenic::MemoryPtr& args,
                          ErrorReporter* error_reporter);

  void Accept(class ResourceVisitor* visitor) override;

  const escher::GpuMemPtr& escher_gpu_mem() const { return escher_gpu_mem_; }

  vk::DeviceSize size() const { return escher_gpu_mem_->size(); }

 private:
  escher::GpuMemPtr escher_gpu_mem_ = nullptr;
};

}  // namespace scene_manager
