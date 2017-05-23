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

class ResourceManager;

// Base class for any resource that must be kept alive until all CommandBuffers
// that reference it have finished executing.
// TODO: named Resource2 to avoid confusion with impl::Resource.  The goal is to
// get rid of impl::Resource, and then rename this class to Resource.
class Resource2 : public Ownable<Resource2, ResourceTypeInfo> {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Call this when this resource must be kept alive until |command_buffer| has
  // completely finished execution.
  void KeepAlive(impl::CommandBuffer* command_buffer);

  // Return the sequence number of the last CommandBuffer that this resource is
  // referenced by.
  uint64_t sequence_number() const { return sequence_number_; }

  // Convenient wrapper around superclass implementation of owner(), since we
  // know that our owner (if any) is always a ResourceManager.
  ResourceManager* owner() const;

  // Return our ResourceManager's VulkanContext.
  const VulkanContext& vulkan_context() const;

 protected:
  explicit Resource2(ResourceManager* owner);

 private:
  uint64_t sequence_number_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(Resource2);
};

typedef ftl::RefPtr<Resource2> Resource2Ptr;

}  // namespace escher
