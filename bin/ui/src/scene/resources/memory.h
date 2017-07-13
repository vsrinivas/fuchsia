// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "apps/mozart/src/scene/resources/resource.h"
#include "apps/mozart/src/scene/util/error_reporter.h"
#include "escher/vk/gpu_mem.h"
#include "lib/mtl/vmo/shared_vmo.h"

namespace mozart {
namespace scene {

// Base class for Resource objects that wrap memory. Subclassed by GpuMemory
// and HostMemory.
class Memory : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

 protected:
  Memory(Session* session, ResourceId id, const ResourceTypeInfo& type_info);
};

using MemoryPtr = ftl::RefPtr<Memory>;

}  // namespace scene
}  // namespace mozart
