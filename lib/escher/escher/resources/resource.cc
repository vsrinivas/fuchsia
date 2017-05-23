// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/resources/resource.h"

#include "escher/impl/command_buffer.h"
#include "escher/resources/resource_manager.h"

namespace escher {

const ResourceTypeInfo Resource2::kTypeInfo("Resource",
                                            ResourceType::kResource);

Resource2::Resource2(ResourceManager* owner) {
  owner->BecomeOwnerOf(this);
}

void Resource2::KeepAlive(impl::CommandBuffer* command_buffer) {
  auto sequence_number = command_buffer->sequence_number();
  FTL_DCHECK(sequence_number_ <= sequence_number);
  sequence_number_ = sequence_number;
}

const VulkanContext& Resource2::vulkan_context() const {
  FTL_DCHECK(owner());
  return owner()->vulkan_context();
}

ResourceManager* Resource2::owner() const {
  return static_cast<ResourceManager*>(
      Ownable<Resource2, ResourceTypeInfo>::owner());
}

}  // namespace escher
