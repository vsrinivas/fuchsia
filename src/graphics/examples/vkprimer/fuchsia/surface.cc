// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "surface.h"

#include "utils.h"

namespace vkp {

Surface::Surface(std::shared_ptr<vk::Instance> instance)
    : initialized_(false), instance_(instance) {}

Surface::~Surface() {
  if (initialized_) {
    vkDestroySurfaceKHR(*instance_, surface_, nullptr);
  }
}

bool Surface::Init() {
  if (initialized_) {
    RTN_MSG(false, "Surface is already initialized.\n");
  }

  // TODO(fxbug.dev/13252): Move to scenic (public) surface.
  VkImagePipeSurfaceCreateInfoFUCHSIA info = {
      .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
      .pNext = nullptr,
  };

  auto rv = vkCreateImagePipeSurfaceFUCHSIA(*instance_, &info, nullptr, &surface_);

  if (rv != VK_SUCCESS) {
    RTN_MSG(false, "Surface creation failed.\n");
  }

  initialized_ = true;
  return true;
}

}  // namespace vkp
