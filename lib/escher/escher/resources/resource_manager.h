// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/owner.h"
#include "escher/forward_declarations.h"
#include "escher/resources/resource.h"
#include "escher/vk/vulkan_context.h"

namespace escher {

// ResourceManager is responsible for deciding whether to reuse or destroy
// resources that are returned to it.  The only restriction is that the
// manager must wait until it is "safe" to destroy the resource; the definition
// of safety depends on the context, but typically means something like not
// destroying resources while they are used by pending Vulkan command-buffers.
//
// Not thread-safe.
class ResourceManager : public Owner<Resource, ResourceTypeInfo> {
 public:
  explicit ResourceManager(Escher* escher);

  Escher* escher() { return escher_; }
  const VulkanContext& vulkan_context() const { return vulkan_context_; }
  vk::Device device() const { return vulkan_context_.device; }

 private:
  friend class Resource;

  // Must be implemented by subclasses.
  virtual void OnReceiveOwnable(std::unique_ptr<Resource> resource) = 0;

  Escher* const escher_;
  VulkanContext vulkan_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ResourceManager);
};

}  // namespace escher
