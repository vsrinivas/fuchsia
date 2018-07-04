// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/resources/resource_manager.h"

#include "lib/escher/escher.h"

namespace escher {

// TODO: DemoHarness::SwapchainImageOwner is currently instantiated before
// an Escher exists.  Fix this, then assert that Escher is non-null here.
ResourceManager::ResourceManager(EscherWeakPtr weak_escher)
    : escher_(std::move(weak_escher)),
      vulkan_context_(escher_ ? escher_->vulkan_context() : VulkanContext()) {}

}  // namespace escher
