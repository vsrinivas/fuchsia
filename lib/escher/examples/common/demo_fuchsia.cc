// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo.h"

void Demo::InitWindowSystem() {}

void Demo::ShutdownWindowSystem() {}

void Demo::SetShouldQuit() {
  should_quit_ = true;
}

void Demo::CreateWindowAndSurface(const WindowParams& params) {
  VkMagmaSurfaceCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_MAGMA_SURFACE_CREATE_INFO_KHR,
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  VkResult err =
      vkCreateMagmaSurfaceKHR(instance_, &create_info, nullptr, &surface);
  FTL_CHECK(!err);
  surface_ = surface;
}

void Demo::AppendPlatformSpecificInstanceExtensionNames(
    InstanceParams* params) {
  params->extension_names.emplace_back(
      std::string(VK_KHR_SURFACE_EXTENSION_NAME));
  params->extension_names.emplace_back(
      std::string(VK_KHR_MAGMA_SURFACE_EXTENSION_NAME));
}

void Demo::PollEvents() {}
