// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "apps/mozart/src/scene/resources/memory.h"
#include "apps/mozart/src/scene/util/error_reporter.h"
#include "escher/vk/gpu_mem.h"
#include "lib/mtl/vmo/shared_vmo.h"

namespace mozart {
namespace scene {

class HostMemory;
typedef ftl::RefPtr<HostMemory> HostMemoryPtr;

// Wraps a CPU host memory-backed VMO.
class HostMemory : public Memory {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Constructor for host memory.
  HostMemory(Session* session, mx::vmo vmo, uint64_t vmo_size);

  // Helper method for creating HostMemory object from a mozart2::Memory.
  // Create a HostMemory resource object from a CPU host memory-backed VMO.
  //
  // Returns the created HostMemory object or nullptr if there was an error.
  static HostMemoryPtr New(Session* session,
                           vk::Device device,
                           const mozart2::MemoryPtr& args,
                           ErrorReporter* error_reporter);

  void Accept(class ResourceVisitor* visitor) override;

  void* memory_base() { return shared_vmo_->Map(); }
  uint64_t size() { return size_; }

 private:
  std::unique_ptr<mtl::SharedVmo> shared_vmo_ = nullptr;
  uint64_t size_;
};

}  // namespace scene
}  // namespace mozart
