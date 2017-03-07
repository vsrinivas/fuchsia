// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/resources/resource_core.h"

namespace escher {

ResourceCore::ResourceCore(ResourceCoreManager* manager) : manager_(manager) {
  FTL_DCHECK(manager_);
  manager_->IncrementResourceCount();
}

ResourceCore::~ResourceCore() {
  manager_->DecrementResourceCount();
}

ResourceCoreManager::ResourceCoreManager(const VulkanContext& context)
    : vulkan_context_(context) {}

ResourceCoreManager::~ResourceCoreManager() {
  FTL_DCHECK(resource_count_ == 0);
}

}  // namespace escher
