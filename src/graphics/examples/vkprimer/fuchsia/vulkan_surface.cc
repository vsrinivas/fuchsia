// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_surface.h"

#include "utils.h"

VulkanSurface::VulkanSurface(std::shared_ptr<VulkanInstance> instance)
    : initialized_(false), instance_(instance) {}

VulkanSurface::~VulkanSurface() {
  if (initialized_) {
    vkDestroySurfaceKHR(*instance_->instance(), surface_, nullptr);
  }
}

bool VulkanSurface::Init() {
  if (initialized_) {
    RTN_MSG(false, "VulkanSurface is already initialized.\n");
  }

  // TODO(fxbug.dev/13252): Move to scenic (public) surface.
  VkImagePipeSurfaceCreateInfoFUCHSIA info = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
      .pNext = nullptr,
  };

  auto rv = vkCreateImagePipeSurfaceFUCHSIA(*instance_->instance(), &info, nullptr, &surface_);

  if (rv != VK_SUCCESS) {
    RTN_MSG(false, "Surface creation failed.\n");
  }

  initialized_ = true;
  return true;
}
