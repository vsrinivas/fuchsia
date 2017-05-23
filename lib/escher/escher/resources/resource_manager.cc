// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/resources/resource_manager.h"

namespace escher {

ResourceManager::ResourceManager(const VulkanContext& context)
    : vulkan_context_(context) {}

}  // namespace escher
