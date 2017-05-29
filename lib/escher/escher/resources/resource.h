// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/ownable.h"
#include "escher/base/owner.h"
#include "escher/forward_declarations.h"
#include "escher/resources/resource_type_info.h"
#include "escher/vk/vulkan_context.h"

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

  // Convenient wrapper around superclass implementation of owner(), since we
  // know that our owner (if any) is always a ResourceManager.
  ResourceManager* owner() const;

  // Return our ResourceManager's VulkanContext.
  const VulkanContext& vulkan_context() const;
  vk::Device device() const { return vulkan_context().device; }

 protected:
  explicit Resource(ResourceManager* owner);

 private:
  // Support CommandBuffer::KeepAlive().
  friend class impl::CommandBuffer;
  void set_sequence_number(uint64_t seq_num) { sequence_number_ = seq_num; }

  uint64_t sequence_number_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(Resource);
};

typedef ftl::RefPtr<Resource> ResourcePtr;

}  // namespace escher
