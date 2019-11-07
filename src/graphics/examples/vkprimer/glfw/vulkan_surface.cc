// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_surface.h"

#include "utils.h"

VulkanSurface::VulkanSurface(std::shared_ptr<VulkanInstance> instance, GLFWwindow *window)
    : initialized_(false), instance_(instance), window_(window) {}

VulkanSurface::~VulkanSurface() {
  if (initialized_) {
    vkDestroySurfaceKHR(*instance_->instance(), surface_, nullptr);
  }
}

bool VulkanSurface::Init() {
  auto rv = glfwCreateWindowSurface(*instance_->instance(), window_, nullptr, &surface_);

  if (rv != VK_SUCCESS) {
    RTN_MSG(false, "GLFW surface creation failed.\n");
  }

  initialized_ = true;
  return true;
}
