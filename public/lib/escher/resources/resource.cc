// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/resources/resource.h"

#include <atomic>

#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/resources/resource_manager.h"

namespace escher {

const ResourceTypeInfo Resource::kTypeInfo("Resource", ResourceType::kResource);

Resource::Resource(ResourceManager* owner)
    : escher_(owner->escher()), uid_(GetUniqueId()) {
  FXL_DCHECK(owner);
  owner->BecomeOwnerOf(this);
}

const VulkanContext& Resource::vulkan_context() const {
  FXL_DCHECK(owner());
  return owner()->vulkan_context();
}

ResourceManager* Resource::owner() const {
  return static_cast<ResourceManager*>(
      Ownable<Resource, ResourceTypeInfo>::owner());
}

uint64_t Resource::GetUniqueId() {
  static std::atomic_uint64_t next_id(0);
  return next_id++;
}

}  // namespace escher
