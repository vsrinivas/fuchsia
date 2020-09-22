// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/resources/resource.h"

#include <atomic>

#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/resources/resource_manager.h"

namespace escher {

const ResourceTypeInfo Resource::kTypeInfo("Resource", ResourceType::kResource);

Resource::Resource(ResourceManager* owner)
    : escher_(owner ? owner->escher() : nullptr), uid_(GetUniqueId()) {
  // TODO(fxbug.dev/7263): It is hard to make a functional ResourceManager in a unit
  // test without bringing up an entire Escher instance. This branch supports
  // some tests, for now, but if it becomes easier to create an owner (i.e. if
  // ResourceManager stops depending on Vulkan and Escher), then this
  // if-statement should be removed.
  if (owner) {
    owner->BecomeOwnerOf(this);
  }
}

const VulkanContext& Resource::vulkan_context() const {
  FX_DCHECK(owner());
  return owner()->vulkan_context();
}

ResourceManager* Resource::owner() const {
  return static_cast<ResourceManager*>(Ownable<Resource, ResourceTypeInfo>::owner());
}

uint64_t Resource::GetUniqueId() {
  static std::atomic_uint64_t next_id(1);
  return next_id++;
}

}  // namespace escher
