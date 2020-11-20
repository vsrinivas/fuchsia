// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "surface.h"

#include "utils.h"

namespace vkp {

Surface::Surface(const std::shared_ptr<vkp::Instance> &vkp_instance, GLFWwindow *window)
    : initialized_(false), vkp_instance_(vkp_instance), window_(window) {}

Surface::~Surface() {
  if (initialized_) {
    vkDestroySurfaceKHR(vkp_instance_->get(), surface_, nullptr);
  }
}

bool Surface::Init() {
  auto rv = glfwCreateWindowSurface(vkp_instance_->get(), window_, nullptr, &surface_);

  if (rv != VK_SUCCESS) {
    RTN_MSG(false, "GLFW surface creation failed.\n");
  }

  initialized_ = true;
  return true;
}

}  // namespace vkp
