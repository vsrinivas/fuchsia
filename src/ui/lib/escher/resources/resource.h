// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RESOURCES_RESOURCE_H_
#define SRC_UI_LIB_ESCHER_RESOURCES_RESOURCE_H_

#include "src/ui/lib/escher/base/ownable.h"
#include "src/ui/lib/escher/base/owner.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/resources/resource_type_info.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"

namespace escher {

namespace impl {
class CommandBuffer;
}  // namespace impl

class ResourceManager;

// Base class for any resource that must be kept alive until all CommandBuffers
// that reference it have finished executing.
class Resource : public Ownable<Resource, ResourceTypeInfo> {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Return the sequence number of the last CommandBuffer that this resource is
  // referenced by.
  uint64_t sequence_number() const { return sequence_number_; }

  // Return our ResourceManager's VulkanContext.
  const VulkanContext& vulkan_context() const;
  vk::Device vk_device() const { return vulkan_context().device; }
  Escher* escher() { return escher_; }

  // Return a unique ID, not shared with any other Resource.
  uint64_t uid() const { return uid_; }

 protected:
  explicit Resource(ResourceManager* owner);

  // Convenient wrapper around superclass implementation of owner(), since we
  // know that our owner (if any) is always a ResourceManager.
  ResourceManager* owner() const;

  // Keep the resource alive until all CommandBuffers up to the specified
  // sequence number have finished executing.
  void KeepAlive(uint64_t seq_num) {
    sequence_number_ = seq_num > sequence_number_ ? seq_num : sequence_number_;
  }

  // Support CommandBuffer::KeepAlive().
  friend class impl::CommandBuffer;

 private:
  // Each resource gets a unique id.
  static uint64_t GetUniqueId();

  Escher* const escher_;
  const uint64_t uid_;
  uint64_t sequence_number_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(Resource);
};

typedef fxl::RefPtr<Resource> ResourcePtr;

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RESOURCES_RESOURCE_H_
